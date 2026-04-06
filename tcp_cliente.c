#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>

#define BUF_SIZE 4096

void erro(char *msg);

int main(int argc, char *argv[]) {
    int fd, n; 
    struct sockaddr_in addr;
    struct hostent *hostPtr;
    char buffer_msg[BUF_SIZE];
    char escolha[10];
	char username[50], password[50];
    if (argc != 3) {
        printf("USO: ./cliente <host> <porto>\n");
        exit(-1);
    }

    // 1. RESOLVER O ENDEREÇO (Faltava esta parte)
    if ((hostPtr = gethostbyname(argv[1])) == 0)
        erro("Nao consegui obter endereço");

    bzero((void *) &addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ((struct in_addr *)(hostPtr->h_addr))->s_addr;
    addr.sin_port = htons((short) atoi(argv[2]));

    if((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        erro("socket");

    // 3. LIGAR AO SERVIDOR (Faltava esta parte)
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        erro("Connect");
    while ((n = read(fd, buffer_msg, BUF_SIZE - 1)) > 0) {
        buffer_msg[n] = '\0';
        printf("%s", buffer_msg);
        fflush(stdout); 
        if (strstr(buffer_msg, "C-cord > ") != NULL) {
            break; 
        }
    }
    if (fgets(escolha, sizeof(escolha), stdin) != NULL) {
        write(fd, escolha, strlen(escolha));
    }
    if((n=read(fd, buffer_msg, BUF_SIZE - 1)) > 0){
		buffer_msg[n] = '\0';
		printf("\n%s",buffer_msg);
		fgets(username, sizeof(username), stdin);
		write(fd, username, strlen(username));
	}
	if((n=read(fd, buffer_msg, BUF_SIZE - 1)) > 0){
		buffer_msg[n] = '\0';
		printf("%s",buffer_msg);
		fgets(password, sizeof(password), stdin);
		write(fd, password, strlen(password));
	}
    

    close(fd);
    return 0;
}

void erro(char *msg) {
    perror(msg);
    exit(-1);
}
