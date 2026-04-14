#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/wait.h>
#include <fcntl.h>

int main(){

    int fildes[2];
    pipe(fildes);
    

    pid_t pid = fork();

    if (pid == 0){
        //FILHO
        close(fildes[1]);
        int msg;
        read(fildes[0], &msg, sizeof(int));
        printf("FILHO: recebi a mensagem %d\n", msg);
        close(fildes[0]);
        _exit(0);
    }else{
        //PAI
        close(fildes[0]);
        int msg = 1234567;
        //sleep(5);
        write(fildes[1], &msg, sizeof(int));
        close(fildes[1]);
        wait(NULL);
          

        
    }
    
}