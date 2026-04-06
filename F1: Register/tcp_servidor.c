#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <sys/wait.h>

#define SERVER_PORT     9000
#define BUF_SIZE        1024

void process_client(int fd);
void erro(char *msg);
void login(int client_fd);  
void registo(int client_fd); 
int main() {
    int fd, client;
    struct sockaddr_in addr, client_addr;
    socklen_t client_addr_size;

    bzero((void *) &addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(SERVER_PORT);

    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
        erro("na funcao socket");

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) 
        erro("na funcao bind");

    if (listen(fd, 5) < 0) 
        erro("na funcao listen");

    client_addr_size = sizeof(client_addr);
    printf("Servidor C-cord à escuta no porto %d...\n", SERVER_PORT);

   while (1) {
        while(waitpid(-1, NULL, WNOHANG) > 0); // Limpa zombies
        
        client = accept(fd, (struct sockaddr *)&client_addr, &client_addr_size);
        
        if (client > 0) {
            if (fork() == 0) {
                close(fd);
                process_client(client);
                exit(0);
            }
            close(client);
        }
        // REMOVIDO O RETURN DAQUI
    }
    
    return 0; // O return fica aqui, fora do loop
}

void process_client(int client_fd) {
    // Bloco 1: Cabeçalho ASCII Limpo
    char msg1[] = "=================================================\n"
                  "    _____            _____              _ \n"
                  "   / ____|          / ____|            | |\n"
                  "  | |      _______ | |     ___  _ __ __| |\n"
                  "  | |     |_______|| |    / _ \\| '__/ _` |\n"
                  "  | |____          | |___| (_) | | | (_| |\n"
                  "   \\_____|          \\_____\\___/|_|  \\__,_|\n"
                  "                The C-Cord Network\n"
                  "=================================================\n";

    // Bloco 2: Opções de Menu
    char msg2[] = "\n[1] Iniciar Sessao\n"
                  "[2] Criar Conta\n"
                  "[0] Desligar\n";

    // Bloco 3: Prompt (Importante manter o espaco para o cliente detectar)
    char msg3[] = "\nC-cord > ";
	char msg4[]=" ";
	char msg5[]=" ";
    // Enviar as mensagens sequencialmente para o cliente
    write(client_fd, msg1, strlen(msg1));
    write(client_fd, msg2, strlen(msg2));
    write(client_fd, msg3, strlen(msg3));

	char buffer[BUF_SIZE];
    char username[50], password[50];
    int nread;
    nread = read(client_fd, buffer, BUF_SIZE - 1);
    if (nread <= 0) return;
    buffer[nread] = '\0';
    int escolha = atoi(buffer);
    switch(escolha){
		case 1:
			login(client_fd);
			break;
		case 2:
			registo(client_fd);
			break;
		default:
			printf("Log: Opção inválida escolhida.\n");
            break;
		}
    
	
    close(client_fd);
}

void login(int client_fd) {
    char username[50], password[50], buffer[BUF_SIZE];
    int nread;

    printf("Log: Iniciando Login...\n");
    
    write(client_fd, "Username: ", 10);
    if ((nread = read(client_fd, buffer, 49)) > 0) {
		buffer[nread-1] = '\0'; 
        strcpy(username, buffer);
    }

    write(client_fd, "Password: ", 10);
    if ((nread = read(client_fd, buffer, 49)) > 0) {
		buffer[nread-1] = '\0'; 
        strcpy(password, buffer);
    }
}
void registo(int client_fd) {
    char username[50], password[50], buffer[BUF_SIZE];
    int nread;

    printf("Log: Iniciando Registo...\n");

    write(client_fd, "Novo Username: ", 15);
    if ((nread = read(client_fd, buffer, 49)) > 0) {
        buffer[nread-1] = '\0'; 
        strcpy(username, buffer);
    }

    write(client_fd, "Nova Password: ", 15);
    if ((nread = read(client_fd, buffer, 49)) > 0) {
        buffer[nread-1] = '\0'; 
        strcpy(password, buffer);
    }
    // 3. Guardar no ficheiro (Persistência F1)
    FILE *fp = fopen("utilizadores.txt", "a");
    if (fp == NULL) {
        write(client_fd, "Erro no servidor (BD).\n", 23);
        return;
    }
    fprintf(fp, "%s:%s\n", username, password);
    fclose(fp);

    printf("Log: Novo utilizador registado: %s\n", username);
    write(client_fd, "Registo concluido com sucesso!\n", 31);
}








void erro(char *msg) {
    printf("Erro: %s\n", msg);
    exit(-1);
}
