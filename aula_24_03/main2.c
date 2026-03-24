#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/wait.h>
#include <fcntl.h>
/*
int main(){

    int fildes[2];
    pipe(fildes);
    

    pid_t pid = fork();

    if (pid == 0){
        //FILHO
        close(fildes[0]);
        int msg = 1234567;
        //sleep(5);
        write(fildes[1], &msg, sizeof(int));
        printf("FILHO: enviei a mensagem %d\n", msg);
        close(fildes[1]);
        _exit(0);
        
    }else{
        //PAI
        close(fildes[1]);
        int msg;
        read(fildes[0], &msg, sizeof(int));
        printf("PAI: recebi a mensagem %d\n", msg);
        close(fildes[0]);
        wait(NULL);
        
        
    }
    
}*/
//segunda versao
#define MAX 100000
int main(){

    int fildes[2];
    pipe(fildes);
    

    pid_t pid = fork();

    if (pid == 0){
        //FILHO
        close(fildes[0]);
        
        for (int i = 0; i < MAX; i++){
            write(fildes[1], &i, sizeof(int));
            printf("FILHO: enviei a mensagem %d\n", i);
        }
        
        close(fildes[1]);
        _exit(0);
        
    }else{
        //PAI
        close(fildes[1]);
        int msg;
        sleep(5);
        for (int i = 0; i < MAX; i++){
            read(fildes[0], &msg, sizeof(int));
            printf("PAI: recebi a mensagem %d\n", msg);
        }
        close(fildes[0]);
        wait(NULL);
        
        
    }
    
}