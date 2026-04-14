#include "matrix.h"


int **createMatrix() {

    // seed random numbers
    srand(123456); // fixed seed
    //srand(time(NULL)); // seed based on time - always changing

    // Allocate and populate matrix with random numbers.
    printf("Generating numbers from 0 to %d...", MAX_RAND);
    int **matrix = (int **) malloc(sizeof(int*) * ROWS);
    for (int i = 0; i < ROWS; i++) {
        matrix[i] = (int*) malloc(sizeof(int) * COLUMNS);
        for (int j = 0; j < COLUMNS; j++) {
            matrix[i][j] = rand() % MAX_RAND;
        }
    }
    printf("Done.\n");

    return matrix;
}

void printMatrix(int **matrix) {

    for (int i = 0; i < ROWS; i++) {
        printf("%2d | ", i);
        for (int j = 0; j < COLUMNS; j++) {
            printf("%7d ", matrix[i][j]);
        }
        printf("\n");
    }
}

void lookupNumber(int** matrix, int value, int* vector){
    printf("PIPE_BUFF: %d\n", PIPE_BUF);
    //TODO
    //Hint - use the Minfo struct from matrix.h!
    pid_t pid;
    
    int fildes[2];
    pipe(fildes);   

    
    //Criar tantos filhos quanto o nº de linhas

    for (int i =0; i<ROWS; i++){
        pid=fork();
        if(pid==0){
            //FILHO
            //Cada filho : conta o nº de occorrencias do value, na sua propria llinha
            //devolve pelo pipe, uma struct mensagem
            close (fildes[0]);
            int counter=0;
            for(int j=0; j<COLUMNS; j++){
                if(matrix[i][j]==value){
                    counter++;
                }
            }
            Minfo msg;

            msg.line_nr=i;
            msg.ocur_nr=counter;
            write(fildes[1], &msg, sizeof(Minfo));
            close (fildes[1]);
            _exit(0);

        }
                
    }
        //PAI
        close(fildes[1]);
        Minfo msg;
        //Pai: lê todas as mensagens do pipe
        
        for (int i=0; i<ROWS; i++){
            read(fildes[0], &msg, sizeof(Minfo));

            printf("PAI: recebi a mensagem do filho %d, linha %d, ocorrencias %d\n", i, msg.line_nr, msg.ocur_nr);
            vector[msg.line_nr]=msg.ocur_nr;
        }
        close(fildes[0]);
        //Pai: espera pelos filhos todos
        for (int i=0; i<ROWS; i++){
            wait(NULL);
        }
        
    

    


}