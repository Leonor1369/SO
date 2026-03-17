#include <unistd.h>

#include <stdio.h>

int main(int argc, char* argv){

    //execl("/bin/ls", "ls", "-l",NULL);

    //perror("ERRO:");
    //execlp("firefox", "firefax",NULL);

    char* args[2];
    args[0]= "ls";
    args[1] = "-l";
    args[2] = NULL;
    //exec
    execvp("ls", args);
    

    return 0;
}