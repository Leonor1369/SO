#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "defs.h"

int main (int argc, char * argv[]){

	if (argc < 2) {
		printf("Missing argument.\n");
		_exit(1);
	}

	//TODO

	int fifo_server = open(SERVER, O_WRONLY);

	//criar o fifo para receber resposta
	char fifo_client_name[20];
	sprintf(fifo_client_name, CLIENT"%d", getpid());
	mkfifo(fifo_client_name, 0600);

	Msg msg;
	msg.needle = atoi(argv[1]);
	write(fifo_server, &msg, sizeof(Msg));
	close(fifo_server);

	/*
	close(fifo_server);

	int fifo_client = open(SERVER, O_RDONLY);
    read(fifo_client, &msg, sizeof(Msg));
    printf("O valor %d ocorre %d vezes no vetor.\n", msg.needle, msg.occurrences);
    close(fifo_client);
	*/
	

	int fifo_client =open(fifo_client_name, O_RDONLY);

	read(fifo_client, &msg, sizeof(Msg));
	close(fifo_client);
	unlink(fifo_client_name);
	printf("Recebi o numero de ocorrencias: %d\n", msg.occurrences);


	return 0;
}