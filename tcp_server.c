/*******************************************************************************
 * SERVIDOR no porto 9000, à escuta de novos clientes.  Quando surgem
 * novos clientes os dados por eles enviados são lidos e descarregados no ecra.
 *******************************************************************************/
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

#define SERVER_PORT     9000
#define BUF_SIZE	1024

// Trata a comunicação de um cliente já aceite.
void process_client(int fd);
// Função utilitária para reportar erros fatais.
void erro(char *msg);

// Servidor TCP concorrente: aceita clientes e mostra no ecrã o texto recebido.
int main() {
  int fd, client;
  struct sockaddr_in addr, client_addr;
  int client_addr_size;

  // bzero(): inicializa a estrutura de endereço com zeros.
  bzero((void *) &addr, sizeof(addr));
  addr.sin_family = AF_INET;
  // INADDR_ANY: escuta em todas as interfaces de rede.
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  // htons(): converte o porto para formato de rede.
  addr.sin_port = htons(SERVER_PORT);

  // socket(): cria socket TCP de escuta.
  if ( (fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	erro("na funcao socket");
  // bind(): associa o socket ao endereço/porto local do servidor.
  if ( bind(fd,(struct sockaddr*)&addr,sizeof(addr)) < 0)
	erro("na funcao bind");
  // listen(): coloca o socket em modo passivo para aceitar ligações.
  if( listen(fd, 5) < 0)
	erro("na funcao listen");
  client_addr_size = sizeof(client_addr);
  while (1) {
    //clean finished child processes, avoiding zombies
    //must use WNOHANG or would block whenever a child process was working
    while(waitpid(-1,NULL,WNOHANG)>0);
    // accept(): aguarda nova ligação e devolve socket dedicado ao cliente.
    client = accept(fd,(struct sockaddr *)&client_addr,(socklen_t *)&client_addr_size);
    if (client > 0) {
      // fork(): cria processo filho para tratar este cliente.
      if (fork() == 0) {
        // No filho, fecha o socket de escuta (não é usado aqui).
        close(fd);
        process_client(client);
        exit(0);
      }
    // No pai, fecha o socket do cliente (o filho já está a tratar).
    close(client);
    }
  }
  return 0;
}

// Lê a mensagem enviada pelo cliente e imprime no terminal do servidor.
void process_client(int client_fd)
{
	int nread = 0;
	char buffer[BUF_SIZE];

	// read(): recebe dados do cliente através da ligação TCP.
	nread = read(client_fd, buffer, BUF_SIZE-1);
	buffer[nread] = '\0';
	printf("%s\n", buffer);

	


	fflush(stdout);

  // close(): encerra a ligação deste cliente.
	close(client_fd);
}

// Mostra mensagem de erro e termina o processo.
void erro(char *msg)
{
	printf("Erro: %s\n", msg);
	exit(-1);
}
