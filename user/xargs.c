#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void run(char* program, char** args)
{
    if(fork()==0)
    {
        exec(program,args);
        exit(0);
    }
    return;
}

int main(int argc, char * argv[])
{
    char buf[2048];
    char *p = buf, * last_p = buf;
    char* argsbuf[128];
    char ** args = argsbuf;// 指向指针的指针，是一个指针变量，他的改变会改变argsbuf中的内容

    // argv为指针数组，所以要创建一个指针数组用来存储其中的内容
    // argv只包含命令行参数，不包含输入，输入只能在下面进行读取
    for(int i = 1;i<argc;i++)
    {
        *args = argv[i];
        args++;
    }

    char ** pa = args;// 类型相同属于是值拷贝，是两个不同的变量，只不过指向了同一个地址，两者之间相互独立

    while(read(0,p,1)!=0)
    {
        if(*p == ' '|| *p == '\n')
        {
            int is_line = (*p=='\n');
            *p = '\0';// 用来分割成独立的字符串，'\0'为字符串结束字符

            *(pa++) = last_p;//此时pa指针指向了输入字符的开始位置，然后指针到下一个char *[]空间
            // 因为是指向数组指针的指针，所以只有确保是一个完整数组的时候，再改
            last_p = p+1;//然后更改last_p到新的位置，即不同字符串分割的位置或者说换行符
            if(is_line)
            {
                *pa = 0;// 因为exec必须要以null即0为结尾，这样才知道具体是在哪里结束了
                run(argv[1],argsbuf);// argsbuf和pa是一致的
                pa = args;// pa重新指向原本的命令行参数之后的第一个位置，进行值的重新覆盖
            }
        }
        p++;
    }

    // 如果最后一行不是空行,因为pa的指针方向会一直随着值的输入进行调整，只有当遇到换行或者为空的时候，才会进行重新开始
    if(pa!=args)
    {
        *p = '\0';
        *(pa++) = last_p;
        *pa = 0;

        run(argv[1],argsbuf);
    }
    while (wait(0)!=-1)
    {
        /* code */
    }
    // echo hello too | xargs echo bye |是管道命令，表示左边命令的标准输出作为右边命令的标准输入
    exit(0);
}