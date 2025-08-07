#include "kernel/param.h"
#include "kernel/fcntl.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "kernel/fs.h"
#include "user/user.h"

void mmap_test();
void fork_test();
void more_test();
char buf[PGSIZE];

#define MAP_FAILED ((char *) -1)

int
main(int argc, char *argv[])
{
  mmap_test();
  fork_test();
  more_test();
  printf("mmaptest: all tests succeeded\n");
  exit(0);
}

void
err(char *why)
{
  printf("mmaptest failure: %s, pid=%d\n", why, getpid());
  exit(1);
}

//
// check the content of the two mapped pages.
//
void
_v1(char *p)
{
  int i;
  for (i = 0; i < PGSIZE*2; i++) {
    if (i < PGSIZE + (PGSIZE/2)) {
      if (p[i] != 'A') {
        printf("mismatch at %d, wanted 'A', got 0x%x\n", i, p[i]);
        err("v1 mismatch (1)");
      }
    } else {
      if (p[i] != 0) {
        printf("mismatch at %d, wanted zero, got 0x%x\n", i, p[i]);
        err("v1 mismatch (2)");
      }
    }
  }
}

//
// create a file to be mapped, containing
// 1.5 pages of 'A' and half a page of zeros.
//
void
makefile(const char *f)
{
  int i;
  int n = PGSIZE/BSIZE;

  unlink(f);
  int fd = open(f, O_WRONLY | O_CREATE);
  if (fd == -1)
    err("open");
  // 一个buf的大小是四分之一页，所以这里实际只分配了1.5页
  memset(buf, 'A', BSIZE);
  // write 1.5 page
  for (i = 0; i < n + n/2; i++) {
    if (write(fd, buf, BSIZE) != BSIZE)
      err("write 0 makefile");
  }
  if (close(fd) == -1)
    err("close");
}

void
mmap_test(void)
{
  int fd;
  int i;
  const char * const f = "mmap.dur";

  //
  // create a file with known content, map it into memory, check that
  // the mapped memory has the same bytes as originally written to the
  // file.
  //
  makefile(f);// 分配1.5页大小的文件
  if ((fd = open(f, O_RDONLY)) == -1)
    err("open (1)");

  printf("test basic mmap\n");
  //
  // this call to mmap() asks the kernel to map the content
  // of open file fd into the address space. the first
  // 0 argument indicates that the kernel should choose the
  // virtual address. the second argument indicates how many
  // bytes to map. the third argument indicates that the
  // mapped memory should be read-only. the fourth argument
  // indicates that, if the process modifies the mapped memory,
  // that the modifications should not be written back to
  // the file nor shared with other processes mapping the
  // same file (of course in this case updates are prohibited
  // due to PROT_READ). the fifth argument is the file descriptor
  // of the file to be mapped. the last argument is the starting
  // offset in the file.
  //
  char *p = mmap(0, PGSIZE*2, PROT_READ, MAP_PRIVATE, fd, 0);
  if (p == MAP_FAILED)
    err("mmap (1)");
  _v1(p);
  if (munmap(p, PGSIZE*2) == -1)
    err("munmap (1)");

  printf("test basic mmap: OK\n");

  printf("test mmap private\n");
  // should be able to map file opened read-only with private writable
  // mapping
  p = mmap(0, PGSIZE*2, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  if (p == MAP_FAILED)
    err("mmap (2)");
  if (close(fd) == -1)
    err("close (1)");
  _v1(p);
  for (i = 0; i < PGSIZE*2; i++)
    p[i] = 'Z';
  if (munmap(p, PGSIZE*2) == -1)
    err("munmap (2)");
  close(fd);

  // file should not have been modified.
  if((fd = open(f, O_RDONLY)) < 0) err("open");
  if(read(fd, buf, PGSIZE) != PGSIZE) err("read");
  if(buf[0] != 'A')
    err("write to MAP_PRIVATE was written to file");
  if(read(fd, buf, PGSIZE) != PGSIZE/2) err("read");
  if(buf[0] != 'A')
    err("write to MAP_PRIVATE was written to file");
  close(fd);

  printf("test mmap private: OK\n");

  printf("test mmap read-only\n");

  // check that mmap doesn't allow read/write mapping of a
  // file opened read-only.
  if ((fd = open(f, O_RDONLY)) == -1)
    err("open (2)");
  p = mmap(0, PGSIZE*2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (p != MAP_FAILED)
    err("mmap (3)");
  if (close(fd) == -1)
    err("close (2)");

  printf("test mmap read-only: OK\n");

  printf("test mmap read/write\n");

  // check that mmap does allow read/write mapping of a
  // file opened read/write.
  if ((fd = open(f, O_RDWR)) == -1)
    err("open (3)");
  p = mmap(0, PGSIZE*3, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (p == MAP_FAILED)
    err("mmap (4)");
  if (close(fd) == -1)
    err("close (3)");

  // check that the mapping still works after close(fd).
  _v1(p);

  // write the mapped memory.
  for (i = 0; i < PGSIZE; i++)
    p[i] = 'B';
  for (i = PGSIZE; i < PGSIZE*2; i++)
    p[i] = 'C';

  // unmap just the first two of three pages of mapped memory.
  // 取消两页映射， 这个时候会把内容写到1.5页的文件当中去
  // 1.5页后面不够的部分好像直接扩充了
  if (munmap(p, PGSIZE*2) == -1)
    err("munmap (3)");

  printf("test mmap read/write: OK\n");

  printf("test mmap dirty\n");

  // check that the writes to the mapped memory were
  // written to the file.
  if ((fd = open(f, O_RDONLY)) == -1)
    err("open (4)");
  if(read(fd, buf, PGSIZE) != PGSIZE)
    err("dirty read #1");
  for (i = 0; i < PGSIZE; i++){
    if (buf[i] != 'B')
      err("file page 0 does not contain modifications");
  }
  // 这里报错感觉是写到文件的问题
  if(read(fd, buf, PGSIZE) != PGSIZE/2)
    err("dirty read #2");
  for (i = 0; i < PGSIZE/2; i++){
    if (buf[i] != 'C')
      err("file page 1 does not contain modifications");
  }
  if (close(fd) == -1)
    err("close (4)");

  printf("test mmap dirty: OK\n");

  printf("test not-mapped unmap\n");

  // unmap the rest of the mapped memory.
  if (munmap(p+PGSIZE*2, PGSIZE) == -1)
    err("munmap (4)");

  printf("test not-mapped unmap: OK\n");

  printf("test lazy access\n");

  if(unlink(f) != 0) err("unlink");
  makefile(f);

  if ((fd = open(f, O_RDWR)) == -1)
    err("open");
  p = mmap(0, PGSIZE*2, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if (p == MAP_FAILED)
    err("mmap");
  close(fd);

  // mmap() should not have read the file at this point,
  // so that the file modification we're about to make
  // ought to be visible to a subsequent read of the
  // mapped memory.

  if((fd = open(f, O_RDWR)) == -1)
    err("open");
  if(write(fd, "m", 1) != 1)
    err("write");
  close(fd);

  if(*p != 'm')
    err("read was not lazy");

  if(munmap(p, PGSIZE*2) == -1)
    err("munmap");

  printf("test lazy access: OK\n");

  printf("test mmap two files\n");

  //
  // mmap two different files at the same time.
  //
  int fd1;
  if((fd1 = open("mmap1", O_RDWR|O_CREATE)) < 0)
    err("open (5)");
  if(write(fd1, "12345", 5) != 5)
    err("write (1)");
  char *p1 = mmap(0, PGSIZE, PROT_READ, MAP_PRIVATE, fd1, 0);
  if(p1 == MAP_FAILED)
    err("mmap (5)");
  if (close(fd1) == -1)
    err("close (5)");
  if (unlink("mmap1") == -1)
    err("unlink (1)");

  int fd2;
  if((fd2 = open("mmap2", O_RDWR|O_CREATE)) < 0)
    err("open (6)");
  if(write(fd2, "67890", 5) != 5)
    err("write (2)");
  char *p2 = mmap(0, PGSIZE, PROT_READ, MAP_PRIVATE, fd2, 0);
  if(p2 == MAP_FAILED)
    err("mmap (6)");
  if (close(fd2) == -1)
    err("close (6)");
  if (unlink("mmap2") == -1)
    err("unlink (2)");

  if(memcmp(p1, "12345", 5) != 0)
    err("mmap1 mismatch");
  if(memcmp(p2, "67890", 5) != 0)
    err("mmap2 mismatch");

  if (munmap(p1, PGSIZE) == -1)
    err("munmap (5)");
  if(memcmp(p2, "67890", 5) != 0)
    err("mmap2 mismatch (2)");
  if (munmap(p2, PGSIZE) == -1)
    err("munmap (6)");

  printf("test mmap two files: OK\n");
}

//
// mmap a file, then fork.
// check that the child sees the mapped file.
//
void
fork_test(void)
{
  int fd;
  int pid;
  const char * const f = "mmap.dur";

  printf("test fork\n");

  // mmap the file twice.
  makefile(f);
  if ((fd = open(f, O_RDONLY)) == -1)
    err("open (7)");
  if (unlink(f) == -1)
    err("unlink (3)");
  char *p1 = mmap(0, PGSIZE*2, PROT_READ, MAP_SHARED, fd, 0);
  if (p1 == MAP_FAILED)
    err("mmap (7)");
  char *p2 = mmap(0, PGSIZE*2, PROT_READ, MAP_SHARED, fd, 0);
  if (p2 == MAP_FAILED)
    err("mmap (8)");

  // read just 2nd page.
  if(*(p1+PGSIZE) != 'A')
    err("fork mismatch (1)");
  printf("match ok\n");
  if((pid = fork()) < 0)
    err("fork");
  // printf("fork ok\n");// 这里也是没问题的
  // printf("pid %d\n", pid);
  if (pid == 0) {
    _v1(p1);
    if (munmap(p1, PGSIZE) == -1) // just the first page
      err("munmap (7)");
    exit(0); // tell the parent that the mapping looks OK, 应该是这里没改
  }
  // 执行到这里也没问题
  int status = -1;
  wait(&status);
  // printf("pid %d\n", pid);
  if(status != 0){
    printf("fork_test failed\n");
    exit(1);
  }

  // check that the parent's mappings are still there.
  _v1(p1);
  _v1(p2);

  printf("test fork: OK\n");
  // printf("=== fork_test end: PID=%d ===\n", getpid());
}

void
more_test()
{
  // printf("=== more_test start: PID=%d ===\n", getpid());
  int fd, pid;
  char *p;
  const char * const f = "mmap.dur";
  
  printf("test munmap prevents access\n");
  
  makefile(f);// 分配1.5页大小的文件
  if ((fd = open(f, O_RDWR)) == -1)
    err("open");
  p = mmap(0, PGSIZE*2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  // 到这里没问题
  if (p == MAP_FAILED)
    err("mmap");
  close(fd);

  *p = 'X';
  *(p+PGSIZE) = 'Y';

  // pid在子进程当中返回的是0
  pid = fork();
  // printf("pid: %d\n",pid);
  if(pid < 0) err("fork");
  // 到这里也没问题
  if(pid == 0){
    *p = 'a';
    *(p+PGSIZE) = 'b';
    // printf("pid : %d\n",pid);
    // printf("Child1 starts: PID=%d, accessing unmapped memory...\n", getpid());
    if(munmap(p+PGSIZE, PGSIZE) == -1)
      err("munmap");
    // this should cause a fatal fault
    // printf("1\n");
    // 这个语句会造成缺页中断
    printf("*(p+PGSIZE) = %x\n", *(p+PGSIZE));
    // printf("Child1 should not reach here: PID=%d\n", getpid());
    exit(0);
  }
  int st = 0;
  wait(&st);
  if(st != -1)
    err("child #1 read unmapped memory");
  // printf("1\n");
  pid = fork();
  // printf("pid: %d\n",pid);
  if(pid < 0) err("fork");
  if(pid == 0){
    *p = 'c';
    *(p+PGSIZE) = 'd';
    // printf("Child1 starts: PID=%d, accessing unmapped memory...\n", getpid());
    if(munmap(p, PGSIZE) == -1)
      err("munmap");
    // this should cause a fatal fault
    printf("*p = %x\n", *p);
    exit(0);
  }
  st = 0;
  wait(&st);
  if(st != -1)
    err("child #2 read unmapped memory");

  // parent should still be able to access the memory.
  // 这里是对父节点的处理，父节点把值写到里面去
  // 怎么这里有两个进程还在执行
  // printf("pid %d\n",getpid());
  *p = 'P';
  *(p+PGSIZE) = 'Q';
  if(munmap(p, PGSIZE) == -1)
    err("munmap");
  *(p+PGSIZE) = 'R';
  if(munmap(p+PGSIZE, PGSIZE) == -1)
    err("munmap");
  // read the file, check that the first page starts
  // with P and the second page with R.
  // 这里还有问题
  // printf("About to read file, current PID: %d\n", getpid());
  fd = open(f, O_RDONLY);
  // struct stat file_stat;
  // fstat(fd, &file_stat);
  // printf("File size: %ld bytes (expected: %d bytes)\n", 
          //  file_stat.size, PGSIZE + PGSIZE/2);
  if(fd < 0) err("open");
  if(read(fd, buf, PGSIZE) != PGSIZE) err("read");
  if(buf[0] != 'P') err("first byte of file is wrong");
  // printf("About to read file, current PID: %d\n", getpid());
  if(read(fd, buf, PGSIZE) != PGSIZE/2) err("read");
  if(buf[0] != 'R') err("first byte of 2nd page of file is wrong");
  close(fd);

  printf("test munmap prevents access: OK\n");

  printf("test writes to read-only mapped memory\n");

  makefile(f);

  pid = fork();
  if(pid < 0) err("fork");
  if(pid == 0){
    if ((fd = open(f, O_RDWR)) == -1)
      err("open");
    p = mmap(0, PGSIZE*2, PROT_READ, MAP_SHARED, fd, 0);
    // printf("About to read file, current PID: %d\n", getpid());
    if (p == MAP_FAILED)
      err("mmap");
    // this should cause a fatal fault
    printf("About to read file, current PID: %d\n", getpid());
    *p = 0;
    printf("About to read file, current PID: %d\n", getpid());
    exit(*p);
  }

  st = 0;
  wait(&st);
  if(st != -1)
    err("child wrote read-only mapping");

  printf("test writes to read-only mapped memory: OK\n");
}
