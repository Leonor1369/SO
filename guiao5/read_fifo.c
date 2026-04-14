#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main(){
    int fifo = open("fifo", O_RDONLY);
    printf("fifo aberto para leitura\n");

    char buf[1024];
    int bytes_read;
    //printf ("%s\n", buf);

    while ((bytes_read = read(fifo, buf, 1024))>0){
        write(1,buf,bytes_read);
    }

    close(fifo);

    return 0;
}