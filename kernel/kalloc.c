// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"


#define PA2PGREF_ID(p) (((p) - KERNBASE) / PGSIZE)
#define PGREF_MAX_NUM PA2PGREF_ID(PHYSTOP)

int page_ref[PGREF_MAX_NUM];

struct spinlock pgreflock;

#define PA2PGREF_NUM(p) page_ref[PA2PGREF_ID((uint64)(p))]

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&pgreflock, "pgref"); //初始化当卡年的自旋锁
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
  // 每次调用kfree的时候减1,只有没有任何一个进程使用的时候，才进行物理页面的释放
  acquire(&pgreflock);
  if(--PA2PGREF_NUM(pa) <= 0)
  {
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
  release(&pgreflock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
  {
    memset((char*)r, 5, PGSIZE); // fill with junk
    PA2PGREF_NUM(r) = 1;
  }
  return (void*)r;
}

void 
krefpg(void *pa)
{
  // 用自旋锁防止竞争
  acquire(&pgreflock);
  PA2PGREF_NUM(pa)++;
  release(&pgreflock);
}

// 写时复制一个新的物理地址返回
// 如果该物理页的引用数>1，则将引用数-1，并分配一个新的物理页返回
// 如果引用数 <=1，则无需操作直接返回该物理页
void *
k_copy(void *pa)
{
  acquire(&pgreflock);

  // 如果当前的物理地址本身就没有其他进程使用，那么就直接返回使用就可以了
  if(PA2PGREF_NUM(pa) <= 1)
  {
    release(&pgreflock);
    return pa;
  }

  uint64 newpa = (uint64)kalloc();
  if(newpa == 0)
  {
    release(&pgreflock);
    return 0;
  }
  memmove((void*) newpa, (void*) pa, PGSIZE);

  PA2PGREF_NUM(pa)--;

  release(&pgreflock);

  return (void *)newpa;
}
