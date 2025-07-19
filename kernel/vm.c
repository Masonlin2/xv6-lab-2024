#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"

/*
 * the kernel's page table,xv6内核的虚拟页表机制
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

#ifdef LAB_NET
  // PCI-E ECAM (configuration space), for pci.c
  kvmmap(kpgtbl, 0x30000000L, 0x30000000L, 0x10000000, PTE_R | PTE_W);

  // pci.c maps the e1000's registers here.
  kvmmap(kpgtbl, 0x40000000L, 0x40000000L, 0x20000, PTE_R | PTE_W);
#endif  

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);
  
  return kpgtbl;
}

// 等同于kvmmap，但是这里专门给进程用 
void
uvmmap(pagetable_t pagetable, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(pagetable, va, sz, pa, perm) != 0)
    panic("uvmmap");
}

void 
mm_kvm_map_pagetable(pagetable_t kpgtbl)
{
  // 将各种内核需要的直接映射添加到 kpgtbl当中，详细看内容地址空间的分配
  // 刚开始只有根目录，kvmmap会进行对应的
  // uart registers
  uvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  uvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // 这个不添加的原因主要是为了放用户地址空间
  // kvmmap(kpgtbl, CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  uvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  uvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  uvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  uvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

// 由于其他进程创建的时候也要使用，模块化并在defs中为kernel全局声明
pagetable_t 
mm_kvminit_newpgtbl()
{
  pagetable_t kpgtbl;
  // 这里只是获取一级根目录，后续根据虚拟地址进行walk扩充
  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  mm_kvm_map_pagetable(kpgtbl);
  return kpgtbl;
}

// Initialize the one kernel_pagetable
// void
// kvminit(void)
// {
//   kernel_pagetable = kvmmake();
// }
void 
kvminit(void)
{
  // 全局内核页表仍然使用他来进行初始化
  kernel_pagetable = mm_kvminit_newpgtbl();
}

// 转换成带偏移量的物理地址
uint64 
kvmpa(pagetable_t pagetable, uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t* pte;
  uint64 pa;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
  {
    panic("kvmap");
  }
  if((*pte & PTE_V) == 0)
  {
    panic("kvmap");
  }
  pa = PTE2PA(*pte);
  return pa + off;

}
// 释放进程的页表映射
void
mm_free_pagetable(pagetable_t pagetable)
{
  for(int i = 0;i < 512;++i)
  {
    pte_t pte = pagetable[i];
    uint64 pa = PTE2PA(pte);
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0)
    {
      mm_free_pagetable((pagetable_t)pa);
      pagetable[i] = 0;// 取消映射
    }
  }
  kfree((void *)pagetable);// 取消当前根目录的映射
}
// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
// 上面讲得是一个虚拟地址的64位的使用
// 该函数返回虚拟地址对应的PTE，alloc为0表示不允许分配新的页表
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");
  // 采用的三级页表，要从最高的level-2开始
  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);// 找此时pte对应的下一级页表，包含的都是物理地址，所以要用物理地址来找
#ifdef LAB_PGTBL
      if(PTE_LEAF(*pte)) {
        return pte;
      }
#endif
    } else {
      // 这里是PTE不存在时，新建一个page，并映射当前的PTE，其实就是下一级的PTE页表
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;// 需要把给出来的物理地址转换一下
    }
  }
  // 上面保证了一定能够给最后一级分配可用的物理地址，下面直接返回调用就可以了
  return &pagetable[PX(0, va)];// 0级即最后一级,返回的是最后一级的pte
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);// 返回对应的最有一级的物理地址PTE
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;// 最后返回的是物理地址
}


// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa.
// va and size MUST be page-aligned.
// Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
// mappages其实做的也只是页表映射，映射的是页表的开头
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if((va % PGSIZE) != 0)// 这里主要是为了页表对齐
    panic("mappages: va not aligned");

  if((size % PGSIZE) != 0)// 这里主要是为了控制大小，保证一页的大小固定
    panic("mappages: size not aligned");

  if(size == 0)
    panic("mappages: size");
  
  a = va;
  last = va + size - PGSIZE;//last是最后一页的起始地址
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)// 表示无效，这里是返回对应的pte或者不存在就创建一个完整的页表项
      return -1;
    if(*pte & PTE_V)// 防止重复映射，就已经有虚拟地址使用当前物理地址了，如果有表示出现异常了，此时要使系统停止，不会向下执行，然后重启
      panic("mappages: remap");
    // walk只是单纯获取，出来要检查是否已经是PTE_V了，PTV表示已经被映射了
    *pte = PA2PTE(pa) | perm | PTE_V;// 创建完且没有重复映射的接着往下执行,给最后一级pte赋值实际的物理地址
    if(a == last)// a为最后一页的时候就不执行了
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;
  int sz;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += sz){
    sz = PGSIZE;
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0) {
      printf("va=%ld pte=%ld\n", a, *pte);
      panic("uvmunmap: not mapped");
    }
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvmfirst(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("uvmfirst: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}


// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
// 这里也是按页分配的。
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;
  int sz;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);// 这里先向上对齐了，考虑到了两个大小在同一页的情况，一次都是按页分，所以在同一页就不需要分配了
  for(a = oldsz; a < newsz; a += sz){
    sz = PGSIZE;
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
#ifndef LAB_SYSCALL
    memset(mem, 0, sz);
#endif
    if(mappages(pagetable, a, sz, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    // 中间节点是不包含(PTE_R|PTE_W|PTE_X) 这三个权限的，所以用来判断是否进行递归
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}
// 递归打印页表
int
pgtprint(pagetable_t pagetable, int depth)
{
  // 三层递归，一层有512个页表项,因为总共27位,然后里面pte的前44位是物理地址的前44位，所以要转换之后才能得到物理地址
  for(int i = 0;i < 512; ++i)
  {
    pte_t pte = pagetable[i];
    if(pte & PTE_V)
    {
      printf("..");
      for(int j = 0;j < depth; ++j)
      {
        printf(" ..");
      }
      printf("%d: pte %p pa %p\n", depth, (void *)pte, (void *)PTE2PA(pte));
      if((pte & (PTE_R|PTE_W|PTE_X)) == 0)
      {
        uint64 child = PTE2PA(pte);
        pgtprint((pagetable_t) child, depth+1);
      }
    }
  }
  return 0;
}
void
mason_vmprint(pagetable_t pagetable)
{
  printf("page table %p\n",pagetable);
  pgtprint(pagetable,0);
}
// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;
  int szinc;

  for(i = 0; i < sz; i += szinc){
    szinc = PGSIZE;
    szinc = PGSIZE;
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    if (va0 >= MAXVA)
      return -1;
    if((pte = walk(pagetable, va0, 0)) == 0) {
      // printf("copyout: pte should exist 0x%x %d\n", dstva, len);
      return -1;
    }


    // forbid copyout over read-only user text pages.
    if((*pte & PTE_W) == 0)
      return -1;
    
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  // uint64 n, va0, pa0;
  
  // while(len > 0){
  //   va0 = PGROUNDDOWN(srcva);// 这是由虚拟地址的组成决定的，后12位是偏移量，用于加到pa0后面的
  //   pa0 = walkaddr(pagetable, va0);
  //   if(pa0 == 0)
  //     return -1;
  //   n = PGSIZE - (srcva - va0);
  //   if(n > len)// 判断此时的读取长度，取当前的最小值
  //     n = len;
  //   memmove(dst, (void *)(pa0 + (srcva - va0)), n);

  //   len -= n;
  //   dst += n;
  //   srcva = va0 + PGSIZE;// va0是当前页的起始地址加上PGSIZE就是下一页的其实地址
  // }
  // return 0;
  return copyin_new(pagetable, dst, srcva, len);
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  // uint64 n, va0, pa0;
  // int got_null = 0;

  // while(got_null == 0 && max > 0){
  //   va0 = PGROUNDDOWN(srcva);
  //   pa0 = walkaddr(pagetable, va0);
  //   if(pa0 == 0)
  //     return -1;
  //   n = PGSIZE - (srcva - va0);
  //   if(n > max)
  //     n = max;

  //   char *p = (char *) (pa0 + (srcva - va0));
  //   while(n > 0){
  //     if(*p == '\0'){
  //       *dst = '\0';
  //       got_null = 1;
  //       break;
  //     } else {
  //       *dst = *p;
  //     }
  //     --n;
  //     --max;
  //     p++;
  //     dst++;
  //   }

  //   srcva = va0 + PGSIZE;
  // }
  // if(got_null){
  //   return 0;
  // } else {
  //   return -1;
  // }
  return copyinstr_new(pagetable, dst, srcva, max);
}

// 将src页表的部分映射关系复制到dst当中，但是不复制实际的物理地址,其实就是把src映射的实际物理地址关系映射到dst上
int
mm_kvmcopymapping(pagetable_t src, pagetable_t dst, uint64 start, uint64 size)
{
  pte_t* pte;
  uint64 pa, i;
  uint flags;
  // PGROUNDUP: 将地址向上取整到页边界，防止重新映射已经映射的页，特别是在执行growproc操作时
  // mappages始终只是对页表进行映射，xv6的映射都是针对页表的，是不考虑虚拟地址的后12位的，映射只是映射整个页表，12位用来确定在当前页表中的具体位置
  // 映射的其实是一个PTE，真正要用的时候找PTE取前44位，然后和自己虚拟地址的12位对应
  for(i = PGROUNDUP(start);i < start+size; i += PGSIZE)
  {
    if( (pte = walk(src,i,0)) == 0)
    {
      panic("kvmcopymapping: pte should exist");
    }
    if( (*pte & PTE_V) == 0)
    {
      panic("kvmcopymapping: page not present");
    }
    flags = PTE_FLAGS(*pte) & ~PTE_U;// 因为内核无法对用户态的页进行直接访问，要修改权限
    pa = PTE2PA(*pte);
    // 用的是同一个dst，因为是同一个根页表，但是不同的虚拟地址一般不会重合，他会对应映射到根页表下面的地方的。
    // 取不同位置主要是用到虚拟地址，而不是该页表，页表都用的是根页表，其他对应改。
    if(mappages(dst, i, PGSIZE, pa, flags) != 0)// 如果有地址是无效的，会返回-1
    {
      goto err;
    }
  }
  return 0;
err:
  // 如果报错了就解除当前dst当前已经添加的所有映射,表示的是此时没有办法映射
  uvmunmap(dst,PGROUNDUP(start),(i - PGROUNDUP(start))/PGSIZE, 0);
  return -1;
}
// 用来缩减进程的虚拟内存空间，释放不再需要的页面
uint64 
mm_dealloc(pagetable_t dst, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
  {
    return oldsz;
  }
  // 由于页面的释放页必须以页位单位，所以要先判断new的页表是不是小于old的页表，在同一页表则不做处理
  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz))
  {
    int npage = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(dst, PGROUNDUP(newsz), npage, 0);
  }
  return newsz;
}
#ifdef LAB_PGTBL
void
vmprint(pagetable_t pagetable) {
  // your code here
}
#endif



#ifdef LAB_PGTBL
pte_t*
pgpte(pagetable_t pagetable, uint64 va) {
  return walk(pagetable, va, 0);
}
#endif
