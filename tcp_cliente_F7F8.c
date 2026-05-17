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

// Cliente com select(): lê teclado e socket ao mesmo tempo.

// Termina o programa quando ocorre um erro crítico.
void erro(char *msg) {
    perror(msg);
    exit(1);
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

    maxfd = (fd > STDIN_FILENO ? fd : STDIN_FILENO);

    while (1) {
        // Espera por dados do servidor ou input do utilizador.
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(fd, &readfds);

        if (select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0)
            erro("select");

        if (FD_ISSET(fd, &readfds)) {
            // Mostra imediatamente mensagens recebidas do servidor.
            n = recv(fd, buffer, BUF_SIZE - 1, 0);
            if (n <= 0) {
                printf("Ligacao terminada pelo servidor.\n");
                break;
            }
            buffer[n] = '\0';
            printf("%s", buffer);
            fflush(stdout);
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            // Lê um comando do teclado e envia-o para o servidor.
            if (fgets(buffer, sizeof(buffer), stdin) == NULL) break;
            if (send(fd, buffer, strlen(buffer), 0) < 0) {
                printf("Erro ao enviar dados.\n");
                break;
            }
            if (strcmp(buffer, "QUIT\n") == 0) {
                /* deixa o servidor responder e fechar do lado dele */
            }
        }
    }

    close(fd);
    return 0;
}
