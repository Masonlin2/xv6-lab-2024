#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
void find(char *path,char* filename)
{
    char buf[512],*p;// p主要是用来对buf进行文件路径处理的
    int fd;
    struct dirent de;
    struct stat st;

    if((fd = open(path, O_RDONLY)) < 0)
    {
        printf("find: can't open %s\n",path);
        return;
    }

    if(fstat(fd, &st) < 0)
    {
        printf("find: can't stat %s\n",path);
    }
    switch(st.type)
    {
        case T_FILE:
        if(strcmp(path+strlen(path)-strlen(filename),filename) == 0)
        {
            printf("%s\n",path);
        }
        break;
        case T_DIR:
        if(strlen(path)+1+DIRSIZ+1 > sizeof buf)// buf用来限制整个文件路进的大小
        {
            printf("find: path too long\n");
            break;
        }

        strcpy(buf,path);
        p = buf+strlen(buf);
        *p++ = '/';// 在末尾加上/，然后再指针指向/的后面
        // 如果是目录那就循环读，把里面文件目录依次读，然后在里面再递归找是否有对应的文件，当前函数即为递归函数
        while(read(fd, &de,sizeof de) ==  sizeof de)
        {
            if(de.inum == 0)
            {
                continue;
            }
            memmove(p,de.name,DIRSIZ);// 刚好把文件名复制到buf的名字的后面
            p[DIRSIZ] = 0;// 在第 DIRSIZ 位置放置 '\0',确保buf是一个有效的C字符串同时防止越界
            if(stat(buf, &st) < 0)
            {
                printf("find: can't stat %s\n",buf);
            }
            if(strcmp(buf+strlen(buf)-2,"/.")!=0 && strcmp(buf+strlen(buf)-3,"/..")!=0)
            {
                find(buf, filename);
            }
        }
        break;
    }
    close(fd);
}

int main(int argc,char ** argv)
{
    if(argc<3)
    {
        printf("find usage: fin <director> <filename>\n");
        exit(1);
    }
    char filename[512];
    filename[0] = '/';
    strcpy(filename+1,argv[2]);
    find(argv[1],filename);
    exit(0);
    // 测试部分$ echo hello world > output.txt ， >符号会进行重定向，是由shell实现的，重定向会创建不存在的文件
    // xv6中的文件都是放在虚拟内存当中的。
}