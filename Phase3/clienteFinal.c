#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#define BUF_SIZE 4096
#define UDP_BUF_SIZE 65000

void erro(char *msg) {
    perror(msg);
    exit(1);
}

static void udp_send_file(const char *ip, int port, const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        printf("[UDP] Ficheiro '%s' nao encontrado.\n>> ", filename);
        fflush(stdout);
        return;
    }

    static char packet[UDP_BUF_SIZE];
    int header_len = snprintf(packet, sizeof(packet), "%s\n", filename);
    int data_len = (int)fread(packet + header_len, 1, sizeof(packet) - header_len - 1, f);
    fclose(f);

    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        printf("[UDP] Erro ao criar socket UDP.\n>> ");
        fflush(stdout);
        return;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        printf("[UDP] IP invalido: %s\n>> ", ip);
        fflush(stdout);
        close(s);
        return;
    }

    if (sendto(s, packet, header_len + data_len, 0,
               (struct sockaddr *)&addr, sizeof(addr)) < 0)
        printf("[UDP] Erro ao enviar ficheiro.\n>> ");
    else
        printf("[UDP] Ficheiro '%s' enviado para %s.\n>> ", filename, ip);
    fflush(stdout);
    close(s);
}

int main(int argc, char *argv[]) {
    int fd, n, maxfd;
    struct sockaddr_in addr;
    struct hostent *hostPtr;
    char buffer[BUF_SIZE];
    fd_set readfds;

    if (argc != 3) {
        printf("USO: ./cliente <host> <porto>\n");
        exit(1);
    }

    if ((hostPtr = gethostbyname(argv[1])) == NULL)
        erro("Nao consegui obter endereco");

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr, hostPtr->h_addr_list[0], hostPtr->h_length);
    addr.sin_port = htons((short)atoi(argv[2]));

    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        erro("socket");

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        erro("connect");

    /* UDP socket for receiving files — bind to port 0 so OS assigns a unique port */
    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    int my_udp_port = 0;
    if (udp_fd >= 0) {
        struct sockaddr_in udp_addr;
        memset(&udp_addr, 0, sizeof(udp_addr));
        udp_addr.sin_family = AF_INET;
        udp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        udp_addr.sin_port = htons(0);
        if (bind(udp_fd, (struct sockaddr *)&udp_addr, sizeof(udp_addr)) < 0) {
            close(udp_fd);
            udp_fd = -1;
            printf("[UDP] Erro ao criar socket UDP — recepcao UDP desactivada.\n");
        } else {
            struct sockaddr_in actual;
            socklen_t alen = sizeof(actual);
            getsockname(udp_fd, (struct sockaddr *)&actual, &alen);
            my_udp_port = ntohs(actual.sin_port);
            printf("[UDP] Recepcao UDP activa na porta %d.\n", my_udp_port);
        }
    }
    /* Inform server of our UDP port so it can relay it to senders */
    if (my_udp_port > 0) {
        char udp_port_msg[32];
        snprintf(udp_port_msg, sizeof(udp_port_msg), "UDP_PORT %d\n", my_udp_port);
        send(fd, udp_port_msg, strlen(udp_port_msg), 0);
    }

    maxfd = fd;
    if (udp_fd > maxfd) maxfd = udp_fd;
    if (STDIN_FILENO > maxfd) maxfd = STDIN_FILENO;

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(fd, &readfds);
        if (udp_fd >= 0) FD_SET(udp_fd, &readfds);

        if (select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0)
            erro("select");

        /* Data from server */
        if (FD_ISSET(fd, &readfds)) {
            n = recv(fd, buffer, BUF_SIZE - 1, 0);
            if (n <= 0) {
                printf("Ligacao terminada pelo servidor.\n");
                break;
            }
            buffer[n] = '\0';

            if (strncmp(buffer, "[UDP_TARGET]", 12) == 0) {
                char ip[INET_ADDRSTRLEN], filename[256];
                int port = 0;
                if (sscanf(buffer + 12, " %45s %d %255s", ip, &port, filename) == 3)
                    udp_send_file(ip, port, filename);
            } else {
                printf("%s", buffer);
                fflush(stdout);
            }
        }

        /* Keyboard input */
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (fgets(buffer, sizeof(buffer), stdin) == NULL) break;
            if (send(fd, buffer, strlen(buffer), 0) < 0) {
                printf("Erro ao enviar dados.\n");
                break;
            }
        }

        /* Incoming UDP file */
        if (udp_fd >= 0 && FD_ISSET(udp_fd, &readfds)) {
            static char udp_buf[UDP_BUF_SIZE];
            struct sockaddr_in sender_addr;
            socklen_t sender_len = sizeof(sender_addr);
            int r = (int)recvfrom(udp_fd, udp_buf, sizeof(udp_buf) - 1, 0,
                                  (struct sockaddr *)&sender_addr, &sender_len);
            if (r > 0) {
                udp_buf[r] = '\0';
                char *newline = strchr(udp_buf, '\n');
                if (newline) {
                    *newline = '\0';
                    char *content = newline + 1;
                    int content_len = r - (int)(content - udp_buf);
                    char saved[300];
                    udp_buf[255] = '\0'; /* cap filename length */
                    snprintf(saved, sizeof(saved), "recebido_%s", udp_buf);
                    FILE *out = fopen(saved, "wb");
                    if (out) {
                        fwrite(content, 1, content_len, out);
                        fclose(out);
                        printf("\n[UDP] Ficheiro recebido: guardado como '%s'\n>> ", saved);
                    } else {
                        printf("\n[UDP] Erro ao guardar ficheiro recebido.\n>> ");
                    }
                    fflush(stdout);
                }
            }
        }
    }

    if (udp_fd >= 0) close(udp_fd);
    close(fd);
    return 0;
}
