#include "mysystem.h"


// recebe um comando por argumento
// returna -1 se o fork falhar
// caso contrario retorna o valor do comando executado
int mysystem (const char* command) {

	int res = -1;
	char* cmd= strdup(command);
	char* token;
	char* args[20];
	int i;
	
	while ((token = strsep(&cmd, " "))!=NULL ){
		//printf ("token: %s\n", token);
		args[i] = token;
		i++;
	}
	args[i]=NULL;

	// TO DO
	//criar um filho
	//o filho executa o comando
	// o pai espera pela execução
	int status;
	pid_t pid = fork();
	if(pid ==0){
		// FILHO

		execvp(args[0], args);
		_exit(-1);
	}else{
		//Pai
		wait(&status);
        if(WIFEXITED(status)){
			res= WIFEXITED(status);
            printf("[PAI] : o filho retornou o valor %d\n",WIFEXITED(status) );

        }else{
            printf("ERRO\n");
        }
	}

	return res;
}