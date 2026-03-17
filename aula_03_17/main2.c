#include <unistd.h>
#include <stdio.h>
#include <stdlib.c>
#include <fcntl.h>
#include <sys/wait.h>
/*
int main(int argc, char* argv[]) {

    pid_t pid = fork();  // Cria processo filho

    if (pid < 0) {
        // Erro ao criar processo
        perror("ERRO no fork");
        return 1;

    } else if (pid == 0) {
        // ---- PROCESSO FILHO ----
        char* args[3];
        args[0] = "ls";
        args[1] = "-l";
        args[2] = NULL;

        execvp("ls", args);

        
        perror("ERRO no execvp");
        return 1;

    } else {
        // ---- PROCESSO PAI ----
        printf("Pai: processo filho criado com PID %d\n", pid);

        wait(NULL);  // Espera que o filho termine

        printf("Pai: filho terminou\n");
    }

    return 0;
}
*/

int main(int argc, char* argv){
    int status;

    pid_t pid; 
    pid=fork();

    if(pid==0){
        //FILHO
        execlp("ls", "ls", "-l", NULL);
        _exit(-1);
    } else{
        //Pai
        wait(&status);
        if(WIFEXITED(status)){
            printf("[PAI] : o filho retornou o valor %d\n",WIFEXITED(status) );

        }else{
            printf("ERRO\n");
        }
    }


    printf("terminou\n");
    return 0;
}