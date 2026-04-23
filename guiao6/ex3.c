#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char* argv[]){


    int new_in = open("/etc/passwd", O_RDONLY);
    int new_out = open("saida.txt", O_WRONLY| O_CREAT, 0600);
    int new_err = open("erros.txt", O_WRONLY| O_CREAT, 0600);

    int y = dup(0);
    int x = dup(1);
    int z = dup(2);

    dup2(new_in, 0);
    dup2(new_out, 1);
    dup2(new_err, 2);

    close(new_in);
    close(new_out);
    close(new_err);
    
    //executar  a syscall exec com o "wc"

    execlp ("wc" , "wc", NULL);


    dup2(y,0);
    dup2(x,1);
    dup2(z,2);

    close(x);
    close(y);
    close(z);

    write(1, "terminei\n", 9);

    return 0;
}