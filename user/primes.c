// 用管道轮询实现对于质数的筛选，主要采用递归
#pragma GCC diagnostic ignored "-Winfinite-recursion"

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
void sieve(int pleft[2])
{
    int p;
    int n = read(pleft[0],&p,sizeof p);
    if(p == -1 || n<=0)
    {
        exit(0);
    }
    fprintf(1,"prime %d\n",p);
    int pright[2];
    pipe(pright);

    int pid = fork();

    if(pid == 0)
    {
        close(pright[1]);
        close(pleft[0]);
        sieve(pright);
    }
    else
    {
        close(pright[0]);
        int buf;
        while(read(pleft[0],&buf,sizeof buf) && buf!=-1)
        {
            if(buf % p!=0)
            {
                write(pright[1],&buf,sizeof buf);
            }
        }
        buf = -1;
        write(pright[1],&buf,sizeof buf);

        wait(0);
        exit(0);
    }

}

int main(int argc, char **argv)
{
    int input_pipe[2];
    pipe(input_pipe);
    int pid = fork();
    if(pid<0)
    {
        close(input_pipe[0]);
        close(input_pipe[1]);
        exit(1);
    }
    else if(pid == 0)
    {
        close(input_pipe[1]);
        sieve(input_pipe);
        exit(0);
    }
    else 
    {
        close(input_pipe[0]);

        for(int i = 2;i<=35;++i)
        {
            write(input_pipe[1],&i,sizeof i);
        }

        int i = -1;
        write(input_pipe[1],&i,sizeof i);
    }
    wait(0);
    exit(0);
}