// 实现父进程和子进程的管道发送
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define RD 0
#define WR 1


int main()
{
    char buf = 'P';
    int fd_c2p[2],fd_p2c[2];

    pipe(fd_p2c);
    pipe(fd_c2p);

    int pid = fork();// fork创建子进程，当前为子进程则返回0,为父进程就返回具体的PID，大于0；
    if(pid<0)
    {
        close(fd_c2p[RD]);
        close(fd_c2p[WR]);
        close(fd_p2c[RD]);
        close(fd_p2c[WR]);
        fprintf(2,"fork create error\n");
        exit(1);
    }
    else if(pid == 0)
    {
        read(fd_p2c[RD],&buf,1);
        // fprintf 和printf的区别是什么，1是有缓冲的，所以不会立刻显示，所以最好用printf
        printf("%d: received ping\n",getpid());

        write(fd_c2p[WR],&buf,1);
        close(fd_c2p[WR]);
    }
    else
    {
        write(fd_p2c[WR],&buf,1);
        close(fd_p2c[WR]);
        
        read(fd_c2p[RD],&buf,1);
        printf("%d: received pong\n",getpid());
        wait(0);
    }
    close(fd_c2p[RD]);
    close(fd_p2c[RD]);
    exit(0);
}