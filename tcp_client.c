/**********************************************************************
 * CLIENTE liga ao servidor (definido em argv[1]) no porto especificado  
 * (em argv[2]), escrevendo a palavra predefinida (em argv[3]).
 * USO: >cliente <enderecoServidor>  <porto>  <Palavra>
 **********************************************************************/
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>

// Função utilitária para reportar erros fatais.
void erro(char *msg);

// Cliente TCP: liga ao servidor e envia a string passada em argv[3].
int main(int argc, char *argv[]) {
  char endServer[100];
  int fd;
  struct sockaddr_in addr;
  struct hostent *hostPtr;

  if (argc != 4) {
    	printf("cliente <host> <port> <string>\n");
    	exit(-1);
  }

  strcpy(endServer, argv[1]);
  // gethostbyname(): resolve o nome do servidor para endereço IP.
  if ((hostPtr = gethostbyname(endServer)) == 0)
    	erro("Nao consegui obter endereço");

  // bzero(): limpa a estrutura de endereço antes de configurar campos.
  bzero((void *) &addr, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = ((struct in_addr *)(hostPtr->h_addr))->s_addr;
  addr.sin_port = htons((short) atoi(argv[2]));

  // socket(): cria socket TCP.
  if((fd = socket(AF_INET,SOCK_STREAM,0)) == -1)
	erro("socket");
  // connect(): estabelece ligação TCP com o servidor.
  if( connect(fd,(struct sockaddr *)&addr,sizeof (addr)) < 0)
	erro("Connect");
  // write(): envia a string ao servidor, incluindo '\0'.
  write(fd, argv[3], 1 + strlen(argv[3]));

  

  // close(): fecha o socket do cliente.
  close(fd);
  exit(0);
}

// Mostra mensagem de erro e termina o processo.
void erro(char *msg) {
	printf("Erro: %s\n", msg);
	exit(-1);
}

