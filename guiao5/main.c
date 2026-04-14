#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>

int main(){
    if (mkfifo("fifo", 0666)!=0){
        perror("fifo");
    }
    return 0;
}