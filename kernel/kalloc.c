// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};
// 为每个CPU分配一个独立的freelist
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

char* kmem_lock_name[] =
{
    "kmem_lock_cpu0",
    "kmem_lock_cpu1",
    "kmem_lock_cpu2",
    "kmem_lock_cpu3",
    "kmem_lock_cpu4",
    "kmem_lock_cpu5",
    "kmem_lock_cpu6",
    "kmem_lock_cpu7",
};
void
kinit()
{
  for(int i = 0;i < NCPU; ++i)
  {
    initlock(&kmem[i].lock, kmem_lock_name[i]);
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  push_off();// 关闭中断
  int cpu = cpuid();
  acquire(&kmem[cpu].lock);
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  release(&kmem[cpu].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  // 加锁避免多个CPU竞争，加锁的区域叫临界区
  push_off();
  int cpu = cpuid();
  acquire(&kmem[cpu].lock);
  // 把其他cpu空闲的内存list移动到当前cpu,先全获取过来，再分配
  // 如果两个cpu互相偷，可能会出现死锁，互相持有对方的锁
  if(!kmem[cpu].freelist)
  {
    int steal_left = 64;
    for(int i = 0; i < NCPU; ++i)
    {
      if(i == cpu)
      {
        continue;
      }
      acquire(&kmem[i].lock);
      if(!kmem[i].freelist)
      {
        release(&kmem[i].lock);
        continue;// 必须要加continue，否则会释放两次锁
      }
      struct run *r_n = kmem[i].freelist;
      while(r_n && steal_left)
      {
        kmem[i].freelist = r_n->next;
        r_n->next = kmem[cpu].freelist;// 把这一个空间摘出来
        kmem[cpu].freelist = r_n;
        r_n = kmem[i].freelist;// 指向下一个
        steal_left--;
      }
      release(&kmem[i].lock);
      if(steal_left == 0)
      {
        break;
      }
    }
  }
  // 此时在为自己获取
  r = kmem[cpu].freelist;
  if(r)
    kmem[cpu].freelist = r->next;
  release(&kmem[cpu].lock);
  pop_off();
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
