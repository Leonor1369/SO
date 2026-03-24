#include <stdio.h>
#include "mysystem.h"

void controller(int N, char** commands) {
	// TO DO
	pid_t pids[N];
	pid_t pid;

	// criar quantos filhos quanto comandos

	for (int  i = 0; i < N; i++){
		// cada filho corre o seu comando, até ele returnar 0(usando a mysystem)
		// o filho retorna quantas vezes o comando correu 
		pid = fork();
		if(pid==0){
			//FILHO
			int counter =0;
			int ret= -1;
			while(ret!=1){
				ret =mysystem(commands[i]);
				counter++;
			}
			_exit(counter);
		}else{
			//PAI
			pids[i] = pid;
		}
	}
	int status;
	for (int i = 0; i < N; i++){
		waitpid(pids[i],&status,0);
		if(WIFEXITED(status)){
			printf("[PAI] o comando %s correu %d vezes\n", commands[i] ,WEXITSTATUS(status));
		} else{
			printf("ERRO\n");
		}
	}
	
	
}

int main(int argc, char* argv[]) {

    char *commands[argc-1];
    int N = 0;
	for(int i=1; i < argc; i++){
		commands[N] = strdup(argv[i]);
		printf("command[%d] = %s\n", N, commands[N]);
        N++;
	}

    controller(N, commands);

	return 0;
}