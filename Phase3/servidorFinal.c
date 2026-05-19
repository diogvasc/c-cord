#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_PORT 9000
#define BUF_SIZE 4096
#define MAX_CLIENTS FD_SETSIZE
#define MAX_NAME 50
#define MAX_CHANNEL 32
#define VERSION "C-cord v2.0"
#define MSG_FILE "mensagens.txt"
#define USER_FILE "utilizadores.txt"

// Estrutura que guarda o estado de cada cliente ligado.

typedef struct {
    int fd;
    int logged_in;
    int admin;
    char username[MAX_NAME];
    char channel[MAX_CHANNEL];
    int state;
    char ip[INET_ADDRSTRLEN];
    int udp_port;                    /* UDP port the client is listening on */
    int udp_pending_sender_fd;      /* fd of sender waiting for our accept */
    char udp_pending_file[256];     /* filename of pending transfer */
} Client;

enum {
    STATE_MENU = 0,
    STATE_LOGIN_USER,
    STATE_LOGIN_PASS,
    STATE_REGISTER_USER,
    STATE_REGISTER_PASS,
    STATE_CHAT,
    STATE_APPROVE
};

time_t server_start_time;
Client clients[MAX_CLIENTS];

static void erro(const char *msg) {
    perror(msg);
    exit(1);
}

// Remove caracteres de nova linha do fim das mensagens recebidas.
static void trim_newline(char *s) {
    if (!s) return;
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == '\r' || s[len-1] == '\n' || s[len-1] == ' ' || s[len-1] == '\t'))
        s[--len] = '\0';
}

static void send_to_client(int fd, const char *msg) {
    if (fd >= 0 && msg) send(fd, msg, strlen(msg), 0);
}

static Client *get_client(int fd) {
    if (fd < 0 || fd >= MAX_CLIENTS) return NULL;
    return &clients[fd];
}

static void reset_client(int fd) {
    if (fd < 0 || fd >= MAX_CLIENTS) return;
    clients[fd].fd = -1;
    clients[fd].logged_in = 0;
    clients[fd].admin = 0;
    clients[fd].username[0] = '\0';
    strcpy(clients[fd].channel, "#general");
    clients[fd].state = STATE_MENU;
    clients[fd].ip[0] = '\0';
    clients[fd].udp_port = 9001;
    clients[fd].udp_pending_sender_fd = -1;
    clients[fd].udp_pending_file[0] = '\0';
}

// Envia o menu inicial ao cliente antes do login/registo.
static void show_initial_menu(int fd) {
    const char *msg =
        "=================================================\n"
        "    _____            _____               _ \n"
        "   / ____|          / ____|             | |\n"
        "  | |      _______ | |     ___  _ __  __| |\n"
        "  | |     |_______|| |    / _ \\| '_ / _ `|\n"
        "  | |____          | |___| (_) || | ( |_| |\n"
        "   \\_____|         \\____\\___/|_|  \\___|\n"
        "                The C-Cord Network\n"
        "=================================================\n"
        "[1] Iniciar Sessao\n"
        "[2] Criar Conta\n"
        "[0] Desligar\n"
        "\nC-cord > ";
    send_to_client(fd, msg);
}

// Mostra os comandos disponíveis depois de autenticar.
static void show_chat_menu(int fd) {
    Client *c = get_client(fd);
    char msg[BUF_SIZE];
    if (!c) return;

    if (c->admin) {
        snprintf(msg, sizeof(msg),
            "\nBem-vindo, %s! Canal atual: %s\n"
            "Comandos disponiveis:\n"
            " LIST_ALL\n VIEW_PENDING_USERS (lista e aprova pendentes)\n DELETE_USER <nome>\n"
            " SEND_MSG <destino> <mensagem>\n CHECK_INBOX\n GET_INFO\n ECHO <mensagem>\n"
            " /join #canal\n /channels\n /who\n /say <mensagem>\n"
            " UDP_SEND <utilizador> <ficheiro>\n UDP_ACCEPT / UDP_REJECT\n QUIT\n\n>> ",
            c->username, c->channel);
    } else {
        snprintf(msg, sizeof(msg),
            "\nBem-vindo, %s! Canal atual: %s\n"
            "Comandos disponiveis:\n"
            " LIST_ALL\n SEND_MSG <destino> <mensagem>\n CHECK_INBOX\n GET_INFO\n ECHO <mensagem>\n"
            " /join #canal\n /channels\n /who\n /say <mensagem>\n"
            " UDP_SEND <utilizador> <ficheiro>\n UDP_ACCEPT / UDP_REJECT\n QUIT\n\n>> ",
            c->username, c->channel);
    }
    send_to_client(fd, msg);
}

// Valida username/password no ficheiro de utilizadores.
static int authenticate_user(const char *username, const char *password, int *admin, int *status) {
    FILE *fp = fopen(USER_FILE, "r");
    char linha[256], u[MAX_NAME], p[MAX_NAME];
    int a = 0, s = 0;
    if (!fp) return -1;
    while (fgets(linha, sizeof(linha), fp)) {
        if (sscanf(linha, "%49[^:]:%49[^:]:%d:%d", u, p, &a, &s) == 4) {
            if (strcmp(u, username) == 0 && strcmp(p, password) == 0) {
                fclose(fp);
                if (admin) *admin = a;
                if (status) *status = s;
                return 1;
            }
        }
    }
    fclose(fp);
    return 0;
}

static int user_exists(const char *username, int *approved, int *admin) {
    FILE *fp = fopen(USER_FILE, "r");
    char linha[256], u[MAX_NAME], p[MAX_NAME];
    int a = 0, s = 0;
    if (!fp) return 0;
    while (fgets(linha, sizeof(linha), fp)) {
        if (sscanf(linha, "%49[^:]:%49[^:]:%d:%d", u, p, &a, &s) == 4 && strcmp(u, username) == 0) {
            fclose(fp);
            if (approved) *approved = s;
            if (admin) *admin = a;
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

// Regista um novo utilizador como pendente de aprovação.
static int register_user(const char *username, const char *password) {
    FILE *fp;
    if (username[0] == '/' || username[0] == '\0') return -2;
    if (user_exists(username, NULL, NULL)) return 0;
    fp = fopen(USER_FILE, "a");
    if (!fp) return -1;
    fprintf(fp, "%s:%s:0:0\n", username, password);
    fclose(fp);
    return 1;
}

// Envia uma mensagem para todos os clientes do mesmo canal.
static void broadcast_channel(const char *channel, int sender_fd, const char *msg) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd != -1 && clients[i].logged_in && clients[i].state == STATE_CHAT && strcmp(clients[i].channel, channel) == 0 && i != sender_fd) {
            send_to_client(i, msg);
        }
    }
}

// Lista todos os utilizadores registados.
static void cmd_list_all(int fd) {
    FILE *fp = fopen(USER_FILE, "r");
    char linha[256], u[MAX_NAME], p[MAX_NAME], out[BUF_SIZE];
    int a = 0, s = 0;
    if (!fp) {
        send_to_client(fd, "Erro ao abrir base de dados.\n>> ");
        return;
    }
    send_to_client(fd, "----- Utilizadores Registados -----\n");
    while (fgets(linha, sizeof(linha), fp)) {
        if (sscanf(linha, "%49[^:]:%49[^:]:%d:%d", u, p, &a, &s) == 4) {
            snprintf(out, sizeof(out), " [%s] %s%s\n", s ? "ATIVO " : "PENDENTE", u, a ? " (admin)" : "");
            send_to_client(fd, out);
        }
    }
    fclose(fp);
    send_to_client(fd, "-----------------------------------\n>> ");
}

// Mostra utilizadores ainda por aprovar e entra em modo de aprovação.
static void cmd_view_pending(int fd) {
    Client *c = get_client(fd);
    FILE *fp = fopen(USER_FILE, "r");
    char linha[256], u[MAX_NAME], p[MAX_NAME], out[BUF_SIZE];
    int a = 0, s = 0, found = 0;
    if (!fp) {
        send_to_client(fd, "Erro ao abrir base de dados.\n>> ");
        return;
    }
    send_to_client(fd, "----- Utilizadores Pendentes -----\n");
    while (fgets(linha, sizeof(linha), fp)) {
        if (sscanf(linha, "%49[^:]:%49[^:]:%d:%d", u, p, &a, &s) == 4 && s == 0) {
            snprintf(out, sizeof(out), " [PENDENTE] %s\n", u);
            send_to_client(fd, out);
            found = 1;
        }
    }
    fclose(fp);
    send_to_client(fd, "-------------------------------\n");
    if (!found) {
        send_to_client(fd, " (sem utilizadores pendentes)\n>> ");
        return;
    }
    c->state = STATE_APPROVE;
    send_to_client(fd, ">> User para aprovar (/Q para sair): ");
}

// Aprova um utilizador pendente (muda status 0->1 no ficheiro).
static int approve_user_in_file(const char *target) {
    FILE *fp = fopen(USER_FILE, "r");
    FILE *tmp = fopen("temp_users.txt", "w");
    char linha[256], u[MAX_NAME], p[MAX_NAME];
    int a = 0, s = 0, found = 0;
    if (!fp || !tmp) {
        if (fp) fclose(fp);
        if (tmp) fclose(tmp);
        return -1;
    }
    while (fgets(linha, sizeof(linha), fp)) {
        if (sscanf(linha, "%49[^:]:%49[^:]:%d:%d", u, p, &a, &s) == 4) {
            if (strcmp(u, target) == 0 && s == 0) {
                fprintf(tmp, "%s:%s:%d:1\n", u, p, a);
                found = 1;
            } else {
                fprintf(tmp, "%s:%s:%d:%d\n", u, p, a, s);
            }
        }
    }
    fclose(fp);
    fclose(tmp);
    if (!found) { remove("temp_users.txt"); return 0; }
    remove(USER_FILE);
    rename("temp_users.txt", USER_FILE);
    return 1;
}

// Apaga um utilizador da base de dados (comando admin).
static void cmd_delete_user(int fd, const char *target) {
    Client *c = get_client(fd);
    FILE *fp, *tmp;
    char linha[256], u[MAX_NAME], p[MAX_NAME];
    int a = 0, s = 0, found = 0;
    if (!target || !*target) {
        send_to_client(fd, "Uso correto: DELETE_USER <nome>\n>> ");
        return;
    }
    if (strcmp(target, c->username) == 0) {
        send_to_client(fd, "Nao pode apagar o proprio utilizador.\n>> ");
        return;
    }
    fp = fopen(USER_FILE, "r");
    tmp = fopen("temp_users.txt", "w");
    if (!fp || !tmp) {
        if (fp) fclose(fp);
        if (tmp) fclose(tmp);
        send_to_client(fd, "Erro a processar ficheiro.\n>> ");
        return;
    }
    while (fgets(linha, sizeof(linha), fp)) {
        if (sscanf(linha, "%49[^:]:%49[^:]:%d:%d", u, p, &a, &s) == 4) {
            if (strcmp(u, target) == 0) found = 1;
            else fprintf(tmp, "%s:%s:%d:%d\n", u, p, a, s);
        }
    }
    fclose(fp);
    fclose(tmp);
    if (!found) {
        remove("temp_users.txt");
        send_to_client(fd, "User nao encontrado.\n>> ");
        return;
    }
    remove(USER_FILE);
    rename("temp_users.txt", USER_FILE);
    send_to_client(fd, "User apagado com sucesso.\n>> ");
}

// Guarda uma mensagem privada para leitura posterior.
static void cmd_send_msg(int fd, const char *cmd) {
    Client *c = get_client(fd);
    char dest[MAX_NAME], msg[BUF_SIZE], out[BUF_SIZE];
    int approved = 0;
    FILE *mf;
    const char *payload = cmd + 8;
    while (*payload == ' ') payload++;
    if (sscanf(payload, "%49s %[^\n]", dest, msg) < 2) {
        send_to_client(fd, "Uso correto: SEND_MSG <destino> <mensagem>\n>> ");
        return;
    }
    if (!user_exists(dest, &approved, NULL) || !approved) {
        send_to_client(fd, "Utilizador nao encontrado ou nao aprovado.\n>> ");
        return;
    }
    if (strcmp(dest, c->username) == 0) {
        send_to_client(fd, "Nao pode enviar mensagem para si mesmo.\n>> ");
        return;
    }
    mf = fopen(MSG_FILE, "a");
    if (!mf) {
        send_to_client(fd, "Erro ao guardar mensagem.\n>> ");
        return;
    }
    fprintf(mf, "%s:%s:%s\n", dest, c->username, msg);
    fclose(mf);
    snprintf(out, sizeof(out), "Mensagem enviada para '%s'.\n>> ", dest);
    send_to_client(fd, out);
}

// Entrega mensagens privadas pendentes ao utilizador.
static void cmd_check_inbox(int fd) {
    Client *c = get_client(fd);
    FILE *fp = fopen(MSG_FILE, "r");
    FILE *outf = fopen("inbox_tmp.txt", "w");
    char linha[BUF_SIZE], dest[MAX_NAME], rem[MAX_NAME], msg[BUF_SIZE], out[BUF_SIZE];
    int found = 0;
    if (!fp || !outf) {
        if (fp) fclose(fp);
        if (outf) fclose(outf);
        send_to_client(fd, "----- Inbox vazia -----\n>> ");
        return;
    }
    send_to_client(fd, "----- A sua Inbox -----\n");
    while (fgets(linha, sizeof(linha), fp)) {
        linha[strcspn(linha, "\n")] = '\0';
        if (sscanf(linha, "%49[^:]:%49[^:]:%[^\n]", dest, rem, msg) == 3) {
            if (strcmp(dest, c->username) == 0) {
                snprintf(out, sizeof(out), " [De: %s] %s\n", rem, msg);
                send_to_client(fd, out);
                found = 1;
            } else {
                fprintf(outf, "%s:%s:%s\n", dest, rem, msg);
            }
        }
    }
    fclose(fp);
    fclose(outf);
    remove(MSG_FILE);
    rename("inbox_tmp.txt", MSG_FILE);
    if (!found) send_to_client(fd, " (sem novas mensagens)\n");
    send_to_client(fd, "-----------------------\n>> ");
}

// Mostra informação básica do servidor.
static void cmd_get_info(int fd) {
    char out[BUF_SIZE];
    time_t now = time(NULL);
    long uptime = (long)(now - server_start_time);
    int active = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) if (clients[i].fd != -1 && clients[i].logged_in) active++;
    snprintf(out, sizeof(out),
             "[INFO] Versao : %s\n[INFO] Uptime : %ldh %ldm %lds\n[INFO] Utilizadores ativos : %d\n>> ",
             VERSION, uptime / 3600, (uptime % 3600) / 60, uptime % 60, active);
    send_to_client(fd, out);
}

// Devolve ao cliente a mensagem enviada.
static void cmd_echo(int fd, const char *cmd) {
    char out[BUF_SIZE];
    const char *msg = cmd + 4;
    while (*msg == ' ') msg++;
    if (*msg == '\0') {
        send_to_client(fd, "Uso correto: ECHO <mensagem>\n>> ");
        return;
    }
    snprintf(out, sizeof(out), "[ECHO] %s\n>> ", msg);
    send_to_client(fd, out);
}

// Lista alguns canais disponíveis.
static void cmd_channels(int fd) {
    send_to_client(fd, "Canais disponiveis: #general, #linux, #games, #redes, #projeto\n>> ");
}

// Mostra os utilizadores presentes no canal atual.
static void cmd_who(int fd) {
    Client *c = get_client(fd);
    char out[BUF_SIZE];
    send_to_client(fd, "Utilizadores no canal:\n");
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd != -1 && clients[i].logged_in && strcmp(clients[i].channel, c->channel) == 0) {
            snprintf(out, sizeof(out), " - %s%s\n", clients[i].username, clients[i].admin ? " (admin)" : "");
            send_to_client(fd, out);
        }
    }
    send_to_client(fd, ">> ");
}

// Move o utilizador para outro canal e notifica os restantes.
static void cmd_join(int fd, const char *cmd) {
    Client *c = get_client(fd);
    char new_channel[MAX_CHANNEL], out[BUF_SIZE], bcast[BUF_SIZE];
    const char *p = cmd + 5;
    while (*p == ' ') p++;
    if (*p == '\0') {
        send_to_client(fd, "Uso correto: /join #canal\n>> ");
        return;
    }
    snprintf(new_channel, sizeof(new_channel), "%s", p);
    trim_newline(new_channel);
    if (new_channel[0] != '#') {
        send_to_client(fd, "O nome do canal deve comecar por #.\n>> ");
        return;
    }
    snprintf(bcast, sizeof(bcast), "\n[SISTEMA][%s] %s saiu do canal.\n>> ", c->channel, c->username);
    broadcast_channel(c->channel, fd, bcast);
    snprintf(c->channel, sizeof(c->channel), "%s", new_channel);
    snprintf(out, sizeof(out), "Entrou no canal %s.\n>> ", c->channel);
    send_to_client(fd, out);
    snprintf(bcast, sizeof(bcast), "\n[SISTEMA][%s] %s entrou no canal.\n>> ", c->channel, c->username);
    broadcast_channel(c->channel, fd, bcast);
}

// Broadcast em tempo real para o canal atual (F7/F8).
static void cmd_say(int fd, const char *cmd) {
    Client *c = get_client(fd);
    char out[BUF_SIZE];
    const char *msg = cmd + 4;
    while (*msg == ' ') msg++;
    if (*msg == '\0') {
        send_to_client(fd, "Uso correto: /say <mensagem>\n>> ");
        return;
    }
    snprintf(out, sizeof(out), "[%s][%s] %s\n>> ", c->channel, c->username, msg);
    broadcast_channel(c->channel, fd, out);
    send_to_client(fd, ">> ");
}

// Pede transferência UDP: notifica destino e aguarda aceitação.
static void cmd_udp_send(int fd, const char *cmd) {
    Client *c = get_client(fd);
    char target[MAX_NAME], filename[256], out[BUF_SIZE];
    const char *p = cmd + 8;
    while (*p == ' ') p++;
    if (sscanf(p, "%49s %255s", target, filename) < 2) {
        send_to_client(fd, "Uso correto: UDP_SEND <utilizador> <ficheiro>\n>> ");
        return;
    }
    if (strcmp(target, c->username) == 0) {
        send_to_client(fd, "Nao pode enviar ficheiro para si mesmo.\n>> ");
        return;
    }
    int target_fd = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd != -1 && clients[i].logged_in &&
            strcmp(clients[i].username, target) == 0) {
            target_fd = i;
            break;
        }
    }
    if (target_fd == -1) {
        send_to_client(fd, "Utilizador nao encontrado ou offline.\n>> ");
        return;
    }
    if (clients[target_fd].udp_pending_sender_fd != -1) {
        send_to_client(fd, "Utilizador ja tem transferencia pendente.\n>> ");
        return;
    }
    clients[target_fd].udp_pending_sender_fd = fd;
    snprintf(clients[target_fd].udp_pending_file,
             sizeof(clients[target_fd].udp_pending_file), "%s", filename);
    snprintf(out, sizeof(out),
             "\n[UDP] %s quer enviar ficheiro '%s'. Aceitar? (UDP_ACCEPT / UDP_REJECT)\n>> ",
             c->username, filename);
    send_to_client(target_fd, out);
    send_to_client(fd, "Pedido enviado. A aguardar resposta...\n>> ");
}

// Aceita transferência UDP pendente.
static void cmd_udp_accept(int fd) {
    Client *c = get_client(fd);
    char out[BUF_SIZE];
    int sender_fd = c->udp_pending_sender_fd;
    if (sender_fd == -1) {
        send_to_client(fd, "Sem transferencia pendente.\n>> ");
        return;
    }
    snprintf(out, sizeof(out), "[UDP_TARGET] %s %d %s\n", c->ip, c->udp_port, c->udp_pending_file);
    send_to_client(sender_fd, out);
    snprintf(out, sizeof(out), "%s aceitou. A enviar ficheiro...\n>> ",  c->username);
    send_to_client(sender_fd, out);
    send_to_client(fd, "Transferencia aceite. A aguardar ficheiro...\n>> ");
    c->udp_pending_sender_fd = -1;
    c->udp_pending_file[0] = '\0';
}

// Rejeita transferência UDP pendente.
static void cmd_udp_reject(int fd) {
    Client *c = get_client(fd);
    char out[BUF_SIZE];
    int sender_fd = c->udp_pending_sender_fd;
    if (sender_fd == -1) {
        send_to_client(fd, "Sem transferencia pendente.\n>> ");
        return;
    }
    snprintf(out, sizeof(out), "%s rejeitou a transferencia.\n>> ", c->username);
    send_to_client(sender_fd, out);
    send_to_client(fd, "Transferencia rejeitada.\n>> ");
    c->udp_pending_sender_fd = -1;
    c->udp_pending_file[0] = '\0';
}

// Remove um cliente desligado do conjunto monitorizado pelo select().
static void disconnect_client(int fd, fd_set *master) {
    Client *c = get_client(fd);
    char out[BUF_SIZE];
    if (!c) return;
    if (c->logged_in) {
        snprintf(out, sizeof(out), "\n[SISTEMA][%s] %s desligou-se.\n>> ", c->channel, c->username);
        broadcast_channel(c->channel, fd, out);
        /* notify any sender waiting on this client */
        if (c->udp_pending_sender_fd != -1) {
            send_to_client(c->udp_pending_sender_fd,
                           "Transferencia cancelada: utilizador desligou-se.\n>> ");
        }
        /* cancel any pending transfer this client initiated */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].udp_pending_sender_fd == fd) {
                send_to_client(i, "Pedido de transferencia cancelado: remetente desligou-se.\n>> ");
                clients[i].udp_pending_sender_fd = -1;
                clients[i].udp_pending_file[0] = '\0';
            }
        }
    }
    close(fd);
    FD_CLR(fd, master);
    reset_client(fd);
}

// Interpreta comandos do utilizador já autenticado.
static void process_chat_command(int fd, char *buffer, fd_set *master) {
    Client *c = get_client(fd);
    if (strcmp(buffer, "LIST_ALL") == 0) cmd_list_all(fd);
    else if (c->admin && strcmp(buffer, "VIEW_PENDING_USERS") == 0) cmd_view_pending(fd);
    else if (c->admin && strncmp(buffer, "DELETE_USER", 11) == 0) {
        const char *p = buffer + 11; while (*p == ' ') p++; cmd_delete_user(fd, p);
    }
    else if (strncmp(buffer, "SEND_MSG", 8) == 0) cmd_send_msg(fd, buffer);
    else if (strcmp(buffer, "CHECK_INBOX") == 0) cmd_check_inbox(fd);
    else if (strcmp(buffer, "GET_INFO") == 0) cmd_get_info(fd);
    else if (strncmp(buffer, "ECHO", 4) == 0) cmd_echo(fd, buffer);
    else if (strcmp(buffer, "/channels") == 0) cmd_channels(fd);
    else if (strcmp(buffer, "/who") == 0) cmd_who(fd);
    else if (strncmp(buffer, "/join", 5) == 0) cmd_join(fd, buffer);
    else if (strncmp(buffer, "/say", 4) == 0) cmd_say(fd, buffer);
    else if (strncmp(buffer, "UDP_SEND", 8) == 0) cmd_udp_send(fd, buffer);
    else if (strcmp(buffer, "UDP_ACCEPT") == 0) cmd_udp_accept(fd);
    else if (strcmp(buffer, "UDP_REJECT") == 0) cmd_udp_reject(fd);
    else if (strcmp(buffer, "QUIT") == 0) {
        send_to_client(fd, "Sessao terminada. Adeus!\n");
        disconnect_client(fd, master);
    } else {
        send_to_client(fd, "Comando desconhecido. Use /say, /join, /channels, /who, LIST_ALL, SEND_MSG, CHECK_INBOX, GET_INFO, ECHO ou QUIT.\n>> ");
    }
}

// Gere os vários estados do cliente: menu, login, registo e chat.
static void process_client_input(int fd, char *buffer, fd_set *master) {
    Client *c = get_client(fd);
    int admin = 0, status = 0, auth = 0, reg = 0;
    char out[BUF_SIZE];
    trim_newline(buffer);
    if (!c) return;

    if (strncmp(buffer, "UDP_PORT ", 9) == 0) {
        int p = atoi(buffer + 9);
        if (p > 0 && p <= 65535) c->udp_port = p;
        return;
    }

    switch (c->state) {
        case STATE_MENU:
            if (strcmp(buffer, "1") == 0) {
                c->state = STATE_LOGIN_USER;
                send_to_client(fd, "Username: ");
            } else if (strcmp(buffer, "2") == 0) {
                c->state = STATE_REGISTER_USER;
                send_to_client(fd, "Novo Username: ");
            } else if (strcmp(buffer, "0") == 0) {
                send_to_client(fd, "A desligar...\n");
                disconnect_client(fd, master);
            } else {
                send_to_client(fd, "Opcao invalida!\n");
                show_initial_menu(fd);
            }
            break;
        case STATE_LOGIN_USER:
            snprintf(c->username, sizeof(c->username), "%s", buffer);
            c->state = STATE_LOGIN_PASS;
            send_to_client(fd, "Password: ");
            break;
        case STATE_LOGIN_PASS:
            auth = authenticate_user(c->username, buffer, &admin, &status);
            if (auth == -1) {
                send_to_client(fd, "Erro no servidor. Tente mais tarde.\n");
                c->state = STATE_MENU;
                show_initial_menu(fd);
            } else if (auth == 0) {
                send_to_client(fd, "Credenciais invalidas.\n");
                c->state = STATE_MENU;
                show_initial_menu(fd);
            } else if (status != 1) {
                send_to_client(fd, "User nao aprovado ainda!\n");
                c->state = STATE_MENU;
                show_initial_menu(fd);
            } else {
                c->logged_in = 1;
                c->admin = admin;
                c->state = STATE_CHAT;
                strcpy(c->channel, "#general");
                snprintf(out, sizeof(out), "\n[SISTEMA][%s] %s entrou no canal.\n>> ", c->channel, c->username);
                broadcast_channel(c->channel, fd, out);
                show_chat_menu(fd);
            }
            break;
        case STATE_REGISTER_USER:
            snprintf(c->username, sizeof(c->username), "%s", buffer);
            c->state = STATE_REGISTER_PASS;
            send_to_client(fd, "Nova Password: ");
            break;
        case STATE_REGISTER_PASS:
            reg = register_user(c->username, buffer);
            if (reg == 1) send_to_client(fd, "Registo concluido com sucesso! Aguarda aprovacao do admin.\n");
            else if (reg == 0) send_to_client(fd, "Erro: Username ja existe.\n");
            else if (reg == -2) send_to_client(fd, "Erro: Username invalido.\n");
            else send_to_client(fd, "Erro no servidor (BD).\n");
            c->state = STATE_MENU;
            c->username[0] = '\0';
            show_initial_menu(fd);
            break;
        case STATE_CHAT:
            process_chat_command(fd, buffer, master);
            break;
        case STATE_APPROVE:
            if (strncmp(buffer, "/Q", 2) == 0 || strncmp(buffer, "/q", 2) == 0) {
                c->state = STATE_CHAT;
                send_to_client(fd, ">> ");
            } else {
                int r = approve_user_in_file(buffer);
                if (r == 1)
                    send_to_client(fd, "Utilizador aprovado!\n>> User para aprovar (/Q para sair): ");
                else if (r == 0)
                    send_to_client(fd, "Erro: user nao encontrado ou ja aprovado.\n>> User para aprovar (/Q para sair): ");
                else
                    send_to_client(fd, "Erro no servidor.\n>> User para aprovar (/Q para sair): ");
            }
            break;
        default:
            c->state = STATE_MENU;
            show_initial_menu(fd);
            break;
    }
}

int main(void) {
    int listener, fdmax;
    struct sockaddr_in addr, client_addr;
    socklen_t addrlen;
    fd_set master, read_fds;

    server_start_time = time(NULL);
    for (int i = 0; i < MAX_CLIENTS; i++) reset_client(i);

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) erro("socket");

    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(SERVER_PORT);

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0) erro("bind");
    if (listen(listener, 10) < 0) erro("listen");

    FD_ZERO(&master);
    FD_ZERO(&read_fds);
    FD_SET(listener, &master);
    fdmax = listener;

    printf("Servidor C-cord (select) a escuta no porto %d...\n", SERVER_PORT);

    while (1) {
        // select() espera por novas ligações ou mensagens em qualquer socket.
        read_fds = master;
        if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) < 0) erro("select");

        for (int i = 0; i <= fdmax; i++) {
            if (!FD_ISSET(i, &read_fds)) continue;

            if (i == listener) {
                // Nova ligação ao servidor.
                int newfd;
                addrlen = sizeof(client_addr);
                newfd = accept(listener, (struct sockaddr *)&client_addr, &addrlen);
                if (newfd < 0) continue;
                if (newfd >= MAX_CLIENTS) {
                    send_to_client(newfd, "Servidor cheio.\n");
                    close(newfd);
                    continue;
                }
                FD_SET(newfd, &master);
                if (newfd > fdmax) fdmax = newfd;
                clients[newfd].fd = newfd;
                clients[newfd].state = STATE_MENU;
                strcpy(clients[newfd].channel, "#general");
                inet_ntop(AF_INET, &client_addr.sin_addr, clients[newfd].ip, INET_ADDRSTRLEN);
                show_initial_menu(newfd);
            } else {
                // Mensagem recebida de um cliente já ligado.
                char buf[BUF_SIZE];
                int nbytes = recv(i, buf, sizeof(buf) - 1, 0);
                if (nbytes <= 0) {
                    disconnect_client(i, &master);
                } else {
                    buf[nbytes] = '\0';
                    process_client_input(i, buf, &master);
                }
            }
        }
    }
    return 0;
}
