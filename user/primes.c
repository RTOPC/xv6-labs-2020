#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

const uint INT_LEN = sizeof(int);

//读取左邻居第一个数
int lpipe_first_data(int lpipe[2], int *dst){
    if(read(lpipe[0], dst, sizeof(int) == sizeof(int))){
        printf("prime %d\n",*dst);
        return 0;
    }
    return -1;
}
//将不能被first整除的写入右邻居
void transmit_data(int lpipe[2], int rpipe[2], int first){
    int data;
    while(read(lpipe[0], &data, sizeof(int) == sizeof(int))){
        //将无法整除的数据传输如右管道
        if(data % first){
            write(rpipe[1], &data, sizeof(int));
        }
    }
    close(lpipe[0]);
    close(rpipe[1]);

}
//左父右子，递归
void primes(int lpipe[2]){
    close(lpipe[1]);
    int first;
    if(lpipe_first_data(lpipe, &first) == 0){
        int p[2];
        pipe(p);
        transmit_data(lpipe, p, first);

        if(fork() == 0){
            primes(p);
        }else{
            close(p[0]);
            wait(0);
        }
    }
    exit(0);
}

int main(int argc, char const *argv[]){
    int p[2];
    pipe(p);
    for(int i = 2; i <= 35; i++){
        write(p[1], &i, sizeof(INT_LEN));
    }
    if(fork() == 0){
        primes(p);
    }else{
        close(p[1]);//        
        close(p[0]);
        wait(0);        
    }
    exit(0);


}