#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "defs.h"
#include "vector.h"

//FIFO criado pelo servidor
//Cliente pode receber um sigpipe (concorrência!)

int main (int argc, char * argv[]){

	init_vector();
	print_vector();

	//TODO
	if ((mkfifo(SERVER, 0666))!=0){
		perror("fifo");
	}

	int fifo_server = open(SERVER, O_RDONLY);

	Msg msg;

	read(fifo_server, &msg, sizeof(Msg));

	printf("Recebi o valor: %d\n", msg.needle);
	/*
	close(fifo_server);

    int fifo_client = open(SERVER, O_WRONLY);
    msg.occurrences = count_needle(msg.needle);
    msg.pid = getpid();
    write(fifo_client, &msg, sizeof(Msg));
    close(fifo_client);
	*/

	//abrir o fifo para resopnder
	char fifo_client_name[20];
	sprintf(fifo_client_name, CLIENT"%d", msg.pid);
	int fifo_client =open(fifo_client_name, O_WRONLY);


	msg.occurrences = count_needle(msg.needle);

	write(fifo_client, &msg, sizeof(Msg));
	return 0;
}