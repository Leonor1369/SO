#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char* argv[]){

    int fildes[2];
    pipe(fildes);

    pid_t pid = fork();
     if (pid == 0){
        //filho
        close(fildes[1]);
        dup2(fildes[0], 0);
        close(fildes[0]);

        execlp("wc", "wc", NULL);
        _exit(0);
    }
    //pai

    close(fildes[0]);

    char buf[1024];
    int bytes_read;
    while((bytes_read = read(0,buf, 1024))> 0){
        write(fildes[1], buf, bytes_read);
    }
    
    close(fildes[1]);
    wait(NULL);

    return 0;
}