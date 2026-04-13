#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>           /* F3: para uptime */

#define SERVER_PORT     9000
#define BUF_SIZE        1024
#define VERSION         "C-cord v1.0"

/* F3: instante em que o servidor arrancou */
time_t server_start_time;

void process_client(int fd);
void erro(char *msg);
void login(int client_fd);
void registo(int client_fd);
void handle_commands(int client_fd, const char *username);  /* F3 */

int main() {
    int fd, client;
    struct sockaddr_in addr, client_addr;
    socklen_t client_addr_size;

    server_start_time = time(NULL);   /* F3: regista o arranque */

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
        while (waitpid(-1, NULL, WNOHANG) > 0); /* limpa zombies */

        client = accept(fd, (struct sockaddr *)&client_addr, &client_addr_size);

        if (client > 0) {
            if (fork() == 0) {
                close(fd);
                process_client(client);
                exit(0);
            }
            close(client);
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
void process_client(int client_fd) {
    char buffer[BUF_SIZE];
    int nread;

    while (1) {
        char msg_menu[] =
            "\n=================================================\n"
            "            The C-Cord Network\n"
            "=================================================\n"
            "[1] Iniciar Sessao\n"
            "[2] Criar Conta\n"
            "[0] Desligar\n"
            "\nC-cord > ";

        // Envia o menu
        write(client_fd, msg_menu, strlen(msg_menu));

        // Lê a opção
        nread = read(client_fd, buffer, BUF_SIZE - 1);
        if (nread <= 0) break; // Cliente desconectou
        buffer[nread] = '\0';

        int escolha = atoi(buffer);
        
        if (escolha == 0) {
            write(client_fd, "A desligar...\n", 14);
            break; 
        }

        switch (escolha) {
            case 1: login(client_fd);   break;
            case 2: registo(client_fd); break;
            default:
                write(client_fd, "Opcao invalida!\n", 16);
                break;
        }
        // Após login() ou registo(), o ciclo recomeça e mostra o menu de novo
    }
    printf("Log: Cliente terminou a sessão.\n");
    close(client_fd);
}

/* ------------------------------------------------------------------ */
void login(int client_fd) {
    char username[50], password[50], buffer[BUF_SIZE];
    int nread;

    printf("Log: Iniciando Login...\n");

    write(client_fd, "Username: ", 10);
    if ((nread = read(client_fd, buffer, 49)) > 0) {
        buffer[nread - 1] = '\0';
        strcpy(username, buffer);
    }

    write(client_fd, "Password: ", 10);
    if ((nread = read(client_fd, buffer, 49)) > 0) {
        buffer[nread - 1] = '\0';
        strcpy(password, buffer);
    }

    //adição para conferir se o login é válido
    FILE *fp = fopen("utilizadores.txt", "r");
    if (fp == NULL) {
        printf("Log: Ficheiro de utilizadores nao encontrado.\n");
        write(client_fd, "Erro no servidor. Tente mais tarde.\n", 36);
        return;
    }

    char linha[110];
    char user_file[50], pass_file[50];
    int encontrado = 0;

    while (fgets(linha, sizeof(linha), fp) != NULL) {
        if (sscanf(linha, "%49[^:]:%49[^\n]", user_file, pass_file) == 2) {
            if (strcmp(user_file, username) == 0 &&
                strcmp(pass_file, password) == 0) {
                encontrado = 1;
                break;
            }
        }
    }
    fclose(fp);

    if (!encontrado) {
        printf("Log: Login falhado para '%s'.\n", username);
        write(client_fd, "Credenciais invalidas. Adeus!\n", 30);
        return;
    }

    printf("Log: Login de '%s' aceite.\n", username);

    /* F3: entra no loop de comandos após autenticação */
    handle_commands(client_fd, username);
}

/* ------------------------------------------------------------------ */
/*  F3 – loop de comandos pós-login                                    */
/* ------------------------------------------------------------------ */
void handle_commands(int client_fd, const char *username) {
    char buffer[BUF_SIZE];
    char response[BUF_SIZE];
    int nread;

    /* Mensagem de boas-vindas com os comandos disponíveis */
    snprintf(response, BUF_SIZE,
        "\nBem-vindo, %s!\n"
        "Comandos disponiveis:\n"
        "  GET_INFO      - informacao do servidor\n"
        "  ECHO <msg>    - devolve a mensagem enviada\n"
        "  QUIT          - terminar sessao\n"
        "\n>> ",
        username);
    write(client_fd, response, strlen(response));

    /* Loop bloqueante de leitura/escrita */
    while ((nread = read(client_fd, buffer, BUF_SIZE - 1)) > 0) {
        buffer[nread] = '\0';

        /* Remove o '\n' final */
        if (nread > 0 && buffer[nread - 1] == '\n')
            buffer[nread - 1] = '\0';

        printf("Log [%s]: '%s'\n", username, buffer);

        /* ---- GET_INFO ---- */
        if (strcmp(buffer, "GET_INFO") == 0) {
            time_t now = time(NULL);
            long uptime_sec = (long)(now - server_start_time);
            long horas  = uptime_sec / 3600;
            long minutos = (uptime_sec % 3600) / 60;
            long segundos = uptime_sec % 60;

            snprintf(response, BUF_SIZE,
                "[INFO] Versao  : %s\n"
                "[INFO] Uptime  : %ldh %ldm %lds\n"
                ">> ",
                VERSION, horas, minutos, segundos);
            write(client_fd, response, strlen(response));

        /* ---- ECHO ---- */
        } else if (strncmp(buffer, "ECHO ", 5) == 0) {
            snprintf(response, BUF_SIZE, "[ECHO] %s\n>> ", buffer + 5);
            write(client_fd, response, strlen(response));

        /* ---- QUIT ---- */
        } else if (strcmp(buffer, "QUIT") == 0) {
            write(client_fd, "Sessao terminada. Adeus!\n", 25);
            printf("Log [%s]: sessao terminada.\n", username);
            break;

        /* ---- Comando desconhecido ---- */
        } else {
            snprintf(response, BUF_SIZE,
                "Comando desconhecido: '%s'\n"
                "Tente GET_INFO, ECHO <msg> ou QUIT.\n>> ",
                buffer);
            write(client_fd, response, strlen(response));
        }
    }
}

/* ------------------------------------------------------------------ */
void registo(int client_fd) {
    char username[50], password[50], buffer[BUF_SIZE];
    int nread;

    printf("Log: Iniciando Registo...\n");

    write(client_fd, "Novo Username: ", 15);
    if ((nread = read(client_fd, buffer, 49)) > 0) {
        buffer[nread - 1] = '\0';
        strcpy(username, buffer);
    }

    write(client_fd, "Nova Password: ", 15);
    if ((nread = read(client_fd, buffer, 49)) > 0) {
        buffer[nread - 1] = '\0';
        strcpy(password, buffer);
    }

    //Adição para evitar utilizadores iguais
    FILE *fp = fopen("utilizadores.txt", "r");
    if (fp != NULL) {
        char linha[110], user_file[50], pass_file[50];
        while (fgets(linha, sizeof(linha), fp)) {
            if (sscanf(linha, "%49[^:]:%49[^\n]", user_file, pass_file) == 2) {
                if (strcmp(username, user_file) == 0) {
                    fclose(fp);
                    write(client_fd, "Erro: Username ja existe.\n", 26);
                    printf("Log: Tentativa de registo duplicado: %s\n", username);
                    return;
                }
            }
        }
        fclose(fp);
    }

    // 3. Guardar no ficheiro (Persistência F1)
    fp = fopen("utilizadores.txt", "a");
    if (fp == NULL) {
        write(client_fd, "Erro no servidor (BD).\n", 23);
        return;
    }
    fprintf(fp, "%s:%s\n", username, password);
    fclose(fp);

    printf("Log: Novo utilizador registado: %s\n", username);
    write(client_fd, "Registo concluido com sucesso!\n", 31);
}

/* ------------------------------------------------------------------ */
void erro(char *msg) {
    printf("Erro: %s\n", msg);
    exit(-1);
}
