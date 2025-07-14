#ifndef SYSINFO_H
#define SYSINFO_H
#include "types.h" 
struct sysinfo {
  uint64 freemem;   // 空闲内存字节数
  uint64 nproc;     // 进程数量
};

#endif