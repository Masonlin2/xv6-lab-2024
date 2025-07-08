// 跟踪系统调用
#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int main(int argc, char *argv[])
{
  int i;
  char *nargv[MAXARG];

  if(argc < 3 || (argv[1][0] < '0' || argv[1][0] > '9')){
    fprintf(2, "Usage: %s mask command\n", argv[0]);
    exit(1);
  }
  // atoi是用来将字符串变成数字的，这个trace会进行汇编包装放到 a0 寄存器当中，然后内核进行获取对应的mask
  // 此时例如当前trace执行是22,但是具体要跟踪的mask是32,所以不会跟踪22
  if (trace(atoi(argv[1])) < 0) {
    fprintf(2, "%s: trace failed\n", argv[0]);
    exit(1);
  }
  // 前面是trace的内容，不在这里进行执行
  for(i = 2; i < argc && i < MAXARG; i++){
    nargv[i-2] = argv[i];
  }
  nargv[argc-2] = 0;// 用于指定结束位
  // exec会替换调当前的进程的程序，但是属性会继承，包括跟踪掩码
  // syscall为系统调用的统一入口，每一次系统调用都会执行
  exec(nargv[0], nargv);
  printf("trace: exec failed\n");
  exit(0);
}
