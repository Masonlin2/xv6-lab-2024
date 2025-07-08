#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"
uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// 获取当前进程的系统调用跟踪掩码
uint64
sys_trace(void)
{
  int mask;
  argint(0,&mask);// 用于指定要跟踪的掩码，trace的系统调用只有一个参数，默认存在第一个

  myproc()->mm_syscall_trace = mask;
  return 0;
}

uint64
sys_sysinfo(void)
{
  struct sysinfo info;
  uint64 addr;
  // 获取信息后的info是在内核空间的，需要用copyout拷贝到用户空间
  mm_freebytes(&info.freemem);

  mm_numproc(&info.nproc);

  argaddr(0, &addr);
  if(copyout(myproc()->pagetable,addr,(char*)&info,sizeof(info))<0)
    return -1;
  return 0;
}
