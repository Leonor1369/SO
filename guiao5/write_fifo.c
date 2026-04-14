#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main(){
    int fifo = open("fifo", O_WRONLY);
    printf("fifo aberto para escrita\n");

    //write(fd, "hello\n", 6);

    char buf[1024];
    int bytes_read;

    while((bytes_read = read(0, buf,1024))>0){
        write(fifo ,buf, bytes_read);
    }

    close(fifo);
    return 0;
}