#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>           

#define SERVER_PORT     9000
#define BUF_SIZE        4096
#define VERSION         "C-cord v1.0"
#define MSG_FILE        "mensagens.txt"   // F4 – ficheiro de mensagens 

time_t server_start_time;

void process_client(int fd);
void erro(char *msg);
void login(int client_fd);
void registo(int client_fd);
void handle_commands(int client_fd, const char *username, int admin);  
void show_menu(int client_fd, const char *username, int admin);

int main() {
    int fd, client;
    struct sockaddr_in addr, client_addr;
    socklen_t client_addr_size;

    server_start_time = time(NULL);   

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
void process_client(int client_fd) {
    char buffer[BUF_SIZE];
    int nread;
    while (1) {
        char msg_menu[] =
        "=================================================\n"
        "    _____            _____              _ \n"
        "   / ____|          / ____|            | |\n"
        "  | |      _______ | |     ___  _ __ __| |\n"
        "  | |     |_______|| |    / _ \\| '__/ _` |\n"
        "  | |____          | |___| (_) | | | (_| |\n"
        "   \\_____|          \\_____\\___/|_|  \\__,_|\n"
        "                The C-Cord Network\n"
        "=================================================\n"
            "[1] Iniciar Sessao\n"
            "[2] Criar Conta\n"
            "[0] Desligar\n"
            "\nC-cord > ";

        write(client_fd, msg_menu, strlen(msg_menu));

        nread = read(client_fd, buffer, BUF_SIZE - 1);
        if (nread <= 0) break;
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
    }
    printf("Log: Cliente terminou a sessão.\n");
    close(client_fd);
}

/* ------------------------------------------------------------------ */
void login(int client_fd) {
    char username[50], password[50], buffer[BUF_SIZE], response[BUF_SIZE];
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

    FILE *fp = fopen("utilizadores.txt", "r");
    if (fp == NULL) {
        printf("Log: Ficheiro de utilizadores nao encontrado.\n");
        write(client_fd, "Erro no servidor. Tente mais tarde.\n", 36);
        return;
    }

    char linha[110];
    char user_file[50], pass_file[50];
    int encontrado = 0;
    int adminis = 0;
    int status = 0;
    while (fgets(linha, sizeof(linha), fp) != NULL) {
        if (sscanf(linha, "%49[^:]:%49[^:]:%d:%d", user_file, pass_file, &adminis, &status) == 4) {
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
        snprintf(response, BUF_SIZE,
        "=================================================\n"
        "Credenciais invalidas. Adeus!\n");
        write(client_fd, response, strlen(response));
        return;
    } else if (status != 1) {
        printf("Log: User não aprovado ainda!\n");
        snprintf(response, BUF_SIZE,
        "=================================================\n"
        "User não aprovado ainda! Tente depois!\n");
        write(client_fd, response, strlen(response));
        return;
    }

    printf("Log: Login de '%s' aceite.\n", username);
    handle_commands(client_fd, username, adminis);
}

// menu 
void handle_commands(int client_fd, const char *username, int admin) {
    char buffer[BUF_SIZE];
    char response[BUF_SIZE];
    int nread;

    //menu pa admin
    if (admin == 1) {
        snprintf(response, BUF_SIZE,
            "\nBem-vindo, %s!\n"
            "Comandos disponiveis:\n"
            "  [A] VIEW PENDING_USERS     - visualizar users em estado [PENDENTE]\n"
            "  [B] DELETE_USER            - apagar utilizador\n"
            "  [C] LIST_ALL               - listar todos os utilizadores\n"
            "  [D] SEND_MSG <user> <msg>  - enviar mensagem a um utilizador\n"
            "  [E] CHECK_INBOX            - ver as suas mensagens\n"
            "  [F] GET_INFO               - informacao do servidor\n"
            "  [G] ECHO <msg>             - devolve a mensagem enviada\n"
            "  [H] QUIT                   - terminar sessao\n"
            "\n>> ", username);
    } else {
        snprintf(response, BUF_SIZE,
            "\nBem-vindo, %s!\n"
            "Comandos disponiveis:\n"
            "  [A] LIST_ALL               - listar todos os utilizadores\n"
            "  [B] SEND_MSG <user> <msg>  - enviar mensagem a um utilizador\n"
            "  [C] CHECK_INBOX            - ver as suas mensagens\n"
            "  [D] GET_INFO               - informacao do servidor\n"
            "  [E] ECHO <msg>             - devolve a mensagem enviada\n"
            "  [F] QUIT                   - terminar sessao\n"
            "\n>> ", username);
    }
    write(client_fd, response, strlen(response));

    while ((nread = read(client_fd, buffer, BUF_SIZE - 1)) > 0) {
        buffer[nread] = '\0';
        if (nread > 0 && buffer[nread - 1] == '\n') buffer[nread - 1] = '\0';

        // log bonito para admin 
        if (admin == 1) {
            if      (strcmp(buffer, "A") == 0) printf("Log [%s]: VIEW PENDING_USERS\n", username);
            else if (strcmp(buffer, "B") == 0) printf("Log [%s]: DELETE_USER\n", username);
            else if (strcmp(buffer, "C") == 0) printf("Log [%s]: LIST_ALL\n", username);
            else if (strncmp(buffer, "D ", 2) == 0 || strcmp(buffer, "D") == 0) printf("Log [%s]: SEND_MSG\n", username);
            else if (strcmp(buffer, "E") == 0) printf("Log [%s]: CHECK_INBOX\n", username);
            else if (strcmp(buffer, "F") == 0) printf("Log [%s]: GET_INFO\n", username);
            else if (strncmp(buffer, "G ", 2) == 0 || strcmp(buffer, "G") == 0) printf("Log [%s]: ECHO\n", username);
            else if (strcmp(buffer, "H") == 0) printf("Log [%s]: QUIT\n", username);
            else printf("Log [%s]: '%s'\n", username, buffer);
        } else {
            if      (strcmp(buffer, "A") == 0) printf("Log [%s]: LIST_ALL\n", username);
            else if (strncmp(buffer, "B ", 2) == 0 || strcmp(buffer, "B") == 0) printf("Log [%s]: SEND_MSG\n", username);
            else if (strcmp(buffer, "C") == 0) printf("Log [%s]: CHECK_INBOX\n", username);
            else if (strcmp(buffer, "D") == 0) printf("Log [%s]: GET_INFO\n", username);
            else if (strncmp(buffer, "E ", 2) == 0 || strcmp(buffer, "E") == 0) printf("Log [%s]: ECHO\n", username);
            else if (strcmp(buffer, "F") == 0) printf("Log [%s]: QUIT\n", username);
            else printf("Log [%s]: '%s'\n", username, buffer);
        }

        // LIST_ALL
        if (strcmp(buffer, "LIST_ALL") == 0 ||
            (admin == 1 && strcmp(buffer, "C") == 0) ||
            (admin != 1 && strcmp(buffer, "A") == 0)) {
            FILE *fp = fopen("utilizadores.txt", "r");
            if (fp == NULL) {
                write(client_fd, "Erro ao abrir base de dados.\n", 29);
                write(client_fd, ">> ", 3);
                continue;
            }
            char linha[110], user_file[50], pass_file[50];
            int adminis = 0, status = 0;
            write(client_fd, "----- Utilizadores Registados -----\n", 36);
            while (fgets(linha, sizeof(linha), fp) != NULL) {
                if (sscanf(linha, "%49[^:]:%49[^:]:%d:%d",
                           user_file, pass_file, &adminis, &status) == 4) {
                    snprintf(response, BUF_SIZE, "  [%s] %s%s\n",
                             status == 1 ? "ATIVO   " : "PENDENTE",
                             user_file,
                             adminis == 1 ? " (admin)" : "");
                    write(client_fd, response, strlen(response));
                }
            }
            fclose(fp);
            write(client_fd, "-----------------------------------\n>> ", 39);

        // SEND_MSG
        } else if (strncmp(buffer, "SEND_MSG ", 9) == 0 ||
               (admin == 1 && (strncmp(buffer, "D ", 2) == 0 || strcmp(buffer, "D") == 0)) ||
               (admin != 1 && (strncmp(buffer, "B ", 2) == 0 || strcmp(buffer, "B") == 0))) {
            /* formato esperado: SEND_MSG <user> <msg> */
            char dest[50], msg[BUF_SIZE];
            const char *payload = buffer + 9;
            if (admin == 1 && (strncmp(buffer, "D ", 2) == 0 || strcmp(buffer, "D") == 0)) payload = buffer + 2;
            if (admin != 1 && (strncmp(buffer, "B ", 2) == 0 || strcmp(buffer, "B") == 0)) payload = buffer + 2;
            if (sscanf(payload, "%49s %[^\n]", dest, msg) < 2) {
                write(client_fd,
                        "Uso correto: SEND_MSG <utilizador> <mensagem>\n>> ",
                        strlen("Uso correto: SEND_MSG <utilizador> <mensagem>\n>> "));
                continue;
            }

            // verificar se o user existe e está aprovado 
            FILE *fp = fopen("utilizadores.txt", "r");
            int dest_ok = 0;
            if (fp) {
                char linha[110], uf[50], pf[50];
                int adm = 0, st = 0;
                while (fgets(linha, sizeof(linha), fp) != NULL) {
                    if (sscanf(linha, "%49[^:]:%49[^:]:%d:%d", uf, pf, &adm, &st) == 4) {
                        if (strcmp(uf, dest) == 0 && st == 1) { dest_ok = 1; break; }
                    }
                }
                fclose(fp);
            }
            if (!dest_ok) {
                snprintf(response, BUF_SIZE,
                         "Utilizador '%s' nao encontrado ou nao aprovado.\n>> ", dest);
                write(client_fd, response, strlen(response));
                continue;
            }
            if (strcmp(dest, username) == 0) {
                write(client_fd, "Nao pode enviar mensagem para si mesmo.\n>> ", 43);
                continue;
            }

            // destinatario:remetente:mensagem 
            FILE *mf = fopen(MSG_FILE, "a");
            if (mf == NULL) {
                write(client_fd, "Erro ao guardar mensagem.\n>> ", 29);
                continue;
            }
            fprintf(mf, "%s:%s:%s\n", dest, username, msg);
            fclose(mf);

            printf("Log [%s]: Mensagem enviada para '%s'.\n", username, dest);
            snprintf(response, BUF_SIZE, "Mensagem enviada para '%s'!\n>> ", dest);
            write(client_fd, response, strlen(response));

        // CHECK_INBOX
        } else if (strcmp(buffer, "CHECK_INBOX") == 0 ||
               (admin == 1 && strcmp(buffer, "E") == 0) ||
               (admin != 1 && strcmp(buffer, "C") == 0)) {
            FILE *fp = fopen(MSG_FILE, "r");
            if (fp == NULL) {
                write(client_fd, "----- Inbox vazia -----\n>> ", 27);
                continue;
            }

            // lê todas as mensagens, mostra as do user atual e re-escreve as restantes num ficheiro temporário
            char tmp_path[] = "inbox_tmp.txt";
            FILE *fout = fopen(tmp_path, "w");
            char linha[BUF_SIZE];
            char dest[50], remetente[50], msg[BUF_SIZE];
            int tem_msgs = 0;

            write(client_fd, "----- A sua Inbox -----\n", 24);
            while (fgets(linha, sizeof(linha), fp) != NULL) {
                // eemove \n do fim para sscanf não o incluir em msg
                linha[strcspn(linha, "\n")] = '\0';
                if (sscanf(linha, "%49[^:]:%49[^:]:%[^\n]", dest, remetente, msg) == 3) {
                    if (strcmp(dest, username) == 0) {
                        // esta mensagem é para o utilizador atual – mostrar 
                        snprintf(response, BUF_SIZE, "  [De: %s] %s\n", remetente, msg);
                        write(client_fd, response, strlen(response));
                        tem_msgs = 1;
                    } else {
                        // mensagem de outro utilizador – manter
                        fprintf(fout, "%s:%s:%s\n", dest, remetente, msg);
                    }
                }
            }
            fclose(fp);
            fclose(fout);

            // substitui o ficheiro original pelo temporário 
            remove(MSG_FILE);
            rename(tmp_path, MSG_FILE);

            if (!tem_msgs)
                write(client_fd, "  (sem novas mensagens)\n", 24);
            write(client_fd, "-----------------------\n>> ", 27);

        // ---- view pending ----
        } else if (admin == 1 && (strcmp(buffer, "VIEW PENDING_USERS") == 0 ||
                       strcmp(buffer, "A") == 0)) {
            char linha[110];
            char user_file[50], pass_file[50];
            int adminis = 0, status = 0;
            char user_aprov[50];

            FILE *fp = fopen("utilizadores.txt", "r");
            if (fp == NULL) {
                write(client_fd, "Erro no servidor. Tente mais tarde.\n", 36);
                continue;
            }
            int i = 0;
            //user:pass:adim:status
            write(client_fd, "----- Utilizadores Pendentes -----\n", 35);
            while (fgets(linha, sizeof(linha), fp) != NULL) {
                if (sscanf(linha, "%49[^:]:%49[^:]:%d:%d",
                           user_file, pass_file, &adminis, &status) == 4) {
                    if (status == 0) {
                        snprintf(response, BUF_SIZE, "[PENDENTE] : %s\n", user_file);
                        write(client_fd, response, strlen(response));
                        i++;
                    }
                }
            }
            fclose(fp);

            if (i == 0) {
                write(client_fd, "\n>>Sem users pendentes de aprovação!\n\n", 40);
                show_menu(client_fd, username, admin);
                continue;
            }
            snprintf(response, BUF_SIZE, "\n>>(Para voltar escreva </Q>)");
            write(client_fd, response, strlen(response));

            while (1) {
                snprintf(response, BUF_SIZE, "\n>> User para aprovar: ");
                write(client_fd, response, strlen(response));
                nread = read(client_fd, user_aprov, 49);
                if (nread <= 0) break;
                user_aprov[nread] = '\0';
                for (int j = 0; j < nread; j++)
                    if (user_aprov[j] == '\n' || user_aprov[j] == '\r') user_aprov[j] = '\0';

                if (strncmp(user_aprov, "/Q", 2) == 0) {
                    show_menu(client_fd, username, admin);
                    break;
                    //handle_commands(client_fd,username);}
                }

                FILE *fp2  = fopen("utilizadores.txt", "r");
                FILE *fout = fopen("temp.txt", "w");
                int justaprov = 0;
                while (fgets(linha, sizeof(linha), fp2) != NULL) {
                    if (sscanf(linha, "%49[^:]:%49[^:]:%d:%d",
                               user_file, pass_file, &adminis, &status) == 4) {
                        if (strcmp(user_aprov, user_file) != 0)
                            fprintf(fout, "%s:%s:%d:%d\n", user_file, pass_file, adminis, status);
                        else {
                            fprintf(fout, "%s:%s:%d:1\n", user_file, pass_file, adminis);
                            justaprov = 1;
                        }
                    }
                }
                fclose(fp2);
                fclose(fout);
                remove("utilizadores.txt");
                rename("temp.txt", "utilizadores.txt");

                if (justaprov) {
                    printf("Log [%s]: Aprovou o user %s\n", username, user_aprov);
                    write(client_fd, "Utilizador aprovado!>>\n", 21);
                } else {
                    write(client_fd, "Erro: Nome nao encontrado na lista!\n", 36);
                }
            }

        // ---- DELETE----
        } else if (admin == 1 && (strncmp(buffer, "DELETE_USER", 11) == 0 ||
                       strcmp(buffer, "B") == 0)) {
            char user_gogo[50];
            char linha[110], user_file[50], pass_file[50];
            int adminis = 0, status = 0;

            FILE *fp = fopen("utilizadores.txt", "r");
            write(client_fd, "----- ALL USERS -----\n", 22);
            while (fgets(linha, sizeof(linha), fp) != NULL) {
                if (sscanf(linha, "%49[^:]:%49[^:]:%d:%d",
                           user_file, pass_file, &adminis, &status) == 4) {
                    if (strcmp(user_file, username) == 0) continue;
                    snprintf(response, BUF_SIZE, "USER : %s\n", user_file);
                    write(client_fd, response, strlen(response));
                }
            }
            fclose(fp);

            while (1) {
                write(client_fd, "\n>> User para apagar (</Q>): ", 28);
                nread = read(client_fd, user_gogo, 49);
                if (nread < 0) break;
                user_gogo[nread - 1] = '\0'; //tirar \n

                if (strncmp(user_gogo, "/Q", 2) == 0) {
                    show_menu(client_fd, username, admin);
                    break;
                }

                //user:pass:adim:status
                FILE *fp2  = fopen("utilizadores.txt", "r");
                FILE *fout = fopen("temp.txt", "w");
                if (fp2 == NULL || fout == NULL) {
                    write(client_fd, "Erro ao processar ficheiro!\n", 28);
                    if (fp2)  fclose(fp2);
                    if (fout) fclose(fout);
                    break;
                }
                int encontrado = 0;
                while (fgets(linha, sizeof(linha), fp2) != NULL) {
                    if (sscanf(linha, "%49[^:]:%49[^:]:%d:%d",
                               user_file, pass_file, &adminis, &status) == 4) {
                        if (strcmp(user_file, user_gogo) != 0)
                            fprintf(fout, "%s:%s:%d:%d\n", user_file, pass_file, adminis, status);
                        else encontrado = 1;
                    }
                }
                fclose(fp2);
                fclose(fout);

                if (encontrado) {
                    remove("utilizadores.txt");
                    rename("temp.txt", "utilizadores.txt");
                    write(client_fd, "User apagado! \n", 13);
                    printf("Log [%s]: Apagou o user %s\n", username, user_gogo);
                } else {
                    remove("temp.txt"); // apaga o ficheiro temporário que não foi usado
                    snprintf(response, BUF_SIZE, "User '%s' não encontrado!\n", user_gogo);
                    write(client_fd, response, strlen(response));
                }
            }

        // ---- GET_INFO ----
        } else if (strcmp(buffer, "GET_INFO") == 0 ||
               (admin == 1 && strcmp(buffer, "F") == 0) ||
               (admin != 1 && strcmp(buffer, "D") == 0)) {
            time_t now = time(NULL);
            long uptime_sec = (long)(now - server_start_time);
            long horas    = uptime_sec / 3600;
            long minutos  = (uptime_sec % 3600) / 60;
            long segundos = uptime_sec % 60;
            snprintf(response, BUF_SIZE,
                "[INFO] Versao  : %s\n"
                "[INFO] Uptime  : %ldh %ldm %lds\n"
                ">> ",
                VERSION, horas, minutos, segundos);
            write(client_fd, response, strlen(response));

        // ---- ECHO ----
         } else if (strncmp(buffer, "ECHO ", 5) == 0 ||
                 (admin == 1 && (strncmp(buffer, "G ", 2) == 0 || strcmp(buffer, "G") == 0)) ||
                 (admin != 1 && (strncmp(buffer, "E ", 2) == 0 || strcmp(buffer, "E") == 0))) {
            const char *echo_msg = buffer + 5;
             if (admin == 1 && (strncmp(buffer, "G ", 2) == 0 || strcmp(buffer, "G") == 0)) echo_msg = buffer + 2;
             if (admin != 1 && (strncmp(buffer, "E ", 2) == 0 || strcmp(buffer, "E") == 0)) echo_msg = buffer + 2;
            snprintf(response, BUF_SIZE, "[ECHO] %s\n>> ", echo_msg);
            write(client_fd, response, strlen(response));

        // ---- QUIT ----
        } else if (strcmp(buffer, "QUIT") == 0 ||
                   (admin == 1 && strcmp(buffer, "H") == 0) ||
                   (admin != 1 && strcmp(buffer, "F") == 0)) {
            write(client_fd, "Sessao terminada. Adeus!\n", 25);
            printf("Log [%s]: sessao terminada.\n", username);
            break;

        // ---- Comando desconhecido ----
        } else {
            snprintf(response, BUF_SIZE,
                "Comando desconhecido: '%s'\n"
                "Tente LIST_ALL, SEND_MSG <user> <msg>, CHECK_INBOX,\n"
                "GET_INFO, ECHO <msg>, QUIT ou atalhos por letra do menu.\n>> ",
                buffer);
            write(client_fd, response, strlen(response));
        }
    }
}

/* ------------------------------------------------------------------ */
void registo(int client_fd) {
    char username[50], password[50], buffer[BUF_SIZE], response[BUF_SIZE];
    int nread;

    printf("Log: Iniciando Registo...\n");

    write(client_fd, "Novo Username: ", 15);
    if ((nread = read(client_fd, buffer, 49)) > 0) {
        buffer[nread - 1] = '\0';
        strcpy(username, buffer);
    }
    if (strncmp(username, "/", 1) == 0) { // para reservar isto para comandos po sistema
        snprintf(response, BUF_SIZE,
                "Usernames não podem começar por </>!\n"
                "Novo Username: ");
        write(client_fd, response, strlen(response));
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
            if (sscanf(linha, "%49[^:]:%49[^:]", user_file, pass_file) == 2) {
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

    fp = fopen("utilizadores.txt", "a");
    if (fp == NULL) {
        write(client_fd, "Erro no servidor (BD).\n", 23);
        return;
    }
    fprintf(fp, "%s:%s:0:0\n", username, password);
    fclose(fp);

    printf("Log: Novo utilizador registado: %s\n", username);
    write(client_fd, "Registo concluido com sucesso!\n", 31);
}

/* ------------------------------------------------------------------ */
void show_menu(int client_fd, const char *username, int admin) {
    char response[BUF_SIZE];

    if (admin == 1) {
        snprintf(response, BUF_SIZE,
            "Comandos disponiveis:\n"
            "  [A] VIEW PENDING_USERS     - visualizar users em estado [PENDENTE]\n"
            "  [B] DELETE_USER            - apagar utilizador\n"
            "  [C] LIST_ALL               - listar todos os utilizadores\n"
            "  [D] SEND_MSG <user> <msg>  - enviar mensagem a um utilizador\n"
            "  [E] CHECK_INBOX            - ver as suas mensagens\n"
            "  [F] GET_INFO               - informacao do servidor\n"
            "  [G] ECHO <msg>             - devolve a mensagem enviada\n"
            "  [H] QUIT                   - terminar sessao\n"
            "\n>> ");
    } else {
        snprintf(response, BUF_SIZE,
            "Comandos disponiveis:\n"
            "  [A] LIST_ALL               - listar todos os utilizadores\n"
            "  [B] SEND_MSG <user> <msg>  - enviar mensagem a um utilizador\n"
            "  [C] CHECK_INBOX            - ver as suas mensagens\n"
            "  [D] GET_INFO               - informacao do servidor\n"
            "  [E] ECHO <msg>             - devolve a mensagem enviada\n"
            "  [F] QUIT                   - terminar sessao\n"
            "\n>> ");
    }
    write(client_fd, response, strlen(response));
}

/* ------------------------------------------------------------------ */
void erro(char *msg) {
    printf("Erro: %s\n", msg);
    exit(-1);
}