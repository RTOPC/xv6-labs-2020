#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char const *argv[]){
    char buf = 'p';
    int p2c[2], c2p[2];

    pipe(p2c);
    pipe(c2p);
    int pid = fork();
    //int pid = fork();
    // if(pid < 0){
    //     printf("fork() error!\n");
    //     close(p2c[0]);
    //     close(p2c[1]);
    //     close(c2p[0]);
    //     close(c2p[1]);
    //     exit(1);
    // }
    // else 
    if(pid == 0){ //child
        // close(p2c[1]);
        // close(c2p[0]);
        read(p2c[0], &buf, sizeof(buf));


        printf("%d: received pong\n", getpid());
        write(c2p[1], &buf, sizeof(buf));
        // close(p2c[0]);
        // close(c2p[1]);
        //exit(0);
    }else{
        // close(p2c[0]);
        // close(c2p[1]);

        write(p2c[1], &buf, sizeof(buf));
        read(c2p[0], &buf, sizeof(buf));
        printf("%d: received ping\n", getpid());

        // close(p2c[1]);
        // close(c2p[0]);
        wait(0);

    }
    exit(0);
}