// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// 哈希桶号的索引数量，质数不易产生竞争
#define NBUFMAP_BUCKET 13
#define BUFMAP_HASH(dev, blockno) ((((dev) << 27)|(blockno))%NBUFMAP_BUCKET) //左移的意义是区分开两者并合并成一个整数，然后塞到桶里面
// 一个磁盘块唯一缓存到一个buf当中
struct {
  // struct spinlock lock;
  // struct buf buf[NBUF];

  // // Linked list of all buffers, through prev/next.
  // // Sorted by how recently the buffer was used.
  // // head.next is most recent, head.prev is least.
  // // 双向链表
  // struct buf head;// 头部本身是一个dummy哨兵节点
  struct buf buf[NBUF];
  // 哈希表桶锁
  struct buf bufmap[NBUFMAP_BUCKET];
  struct spinlock bufmap_locks[NBUFMAP_BUCKET];// 哈希桶，里面放的其实就是一个链表
  struct spinlock eviction; // 驱逐锁
} bcache;

char* bcache_bufmap[] = 
{
  "bcache_bufmap0",
  "bcache_bufmap1",
  "bcache_bufmap2",
  "bcache_bufmap3",
  "bcache_bufmap4",
  "bcache_bufmap5",
  "bcache_bufmap6",
  "bcache_bufmap7",
  "bcache_bufmap8",
  "bcache_bufmap9",
  "bcache_bufmap10",
  "bcache_bufmap11",
  "bcache_bufmap12"
};
void
binit(void)
{
  for(int i = 0;i < NBUFMAP_BUCKET; ++i)
  {
    initlock(&bcache.bufmap_locks[i], bcache_bufmap[i]);
    bcache.bufmap[i].next = 0;// 不写i会导致只有第一个即bufmap[0]初始化
  }

  // Create linked list of buffers
  for(int i = 0; i < NBUF; ++i)
  {
    struct buf *b = &bcache.buf[i];
    initsleeplock(&b->lock,"buffer");
    b->refcnt = 0;
    b->last_used = 0;

    // 把所有buf的缓存块先默认存到bufmap[0]当中
    b->next = bcache.bufmap[0].next;
    bcache.bufmap[0].next = b;
  }
  initlock(&bcache.eviction, "bcache_eviction");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  uint key = BUFMAP_HASH(dev, blockno);

  acquire(&bcache.bufmap_locks[key]);

  // 因为第一个是dummy
  for(b = bcache.bufmap[key].next; b; b = b->next)
  {
    if(b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++;// 复用
      release(&bcache.bufmap_locks[key]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 去除当前的锁，避免死锁，同时加上新的全局锁来避免竞争的产生
  release(&bcache.bufmap_locks[key]);
  acquire(&bcache.eviction);

  // 在释放桶锁和增加驱逐锁的间隙可能会产生缓存块，所以要再检查一遍
  for(b = bcache.bufmap[key].next; b; b = b->next)
  {
    if(b->dev == dev && b->blockno == blockno)
    {
      acquire(&bcache.bufmap_locks[key]);
      b->refcnt++;// 复用
      release(&bcache.bufmap_locks[key]);
      release(&bcache.eviction);
      acquiresleep(&b->lock);
      return b;
    }
  }

  struct buf* before_least = 0; //LRU-buf的前一个块
  uint holding_bucket = -1; // 当前持有哪个桶锁

  for(int i = 0; i < NBUFMAP_BUCKET; ++i)
  {
    int newfound = 0;
    acquire(&bcache.bufmap_locks[i]);
    for(b = &bcache.bufmap[i]; b->next; b = b->next)
    {
      // 找最近久未使用的, 被释放的时间越早，则越久没使用
      if(b->next->refcnt == 0 && (!before_least || before_least->next->last_used > b->next->last_used))
      {
        before_least = b;
        newfound = 1;// 不能写=i，因为i是从0开始的，如果i的话，那所有第一个哈希桶存的都拿不到
      }
    }

    if(!newfound)// 如果没有找到则要释放当前的锁
    {
      release(&bcache.bufmap_locks[i]);
    }
    else
    {
      if(holding_bucket != -1)// 如果找到了但是不是第一个LRU-buf，之前一定持有某个桶锁，要进行释放，主要是这里可能会找到更老的LRU，在整个的遍历过程当中
      {
        release(&bcache.bufmap_locks[holding_bucket]);
      }
      holding_bucket = i;
    }
  }
  if(!before_least)
  {
    panic("bget: no buffers");
  }
  b = before_least->next;
  // 如果此时buf所在的不是我们需要的key的位置，那要转移过去，这和我们刚开始所有buf初始化在0的位置有关系
  if(holding_bucket != key)
  {
    before_least->next = b->next;// 从当前释放出去
    release(&bcache.bufmap_locks[holding_bucket]);
    acquire(&bcache.bufmap_locks[key]);
    // 每次都是放在开头的位置
    b->next = bcache.bufmap[key].next;
    bcache.bufmap[key].next = b;
  }

  // 设置新的buf字段
  b->dev = dev;
  b->blockno = blockno;
  b->refcnt = 1;
  b->valid = 0;

  // 此时再释放锁
  release(&bcache.bufmap_locks[key]);
  release(&bcache.eviction);
  acquiresleep(&b->lock);
  return b;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");
  releasesleep(&b->lock);

  int key = BUFMAP_HASH(b->dev, b->blockno);
  acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    // 这里不需要维护一个LRU链表，只需要通过ticks去进行控制就行
    b->last_used = ticks;
  }
  release(&bcache.bufmap_locks[key]);
}

void
bpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno);
  acquire(&bcache.bufmap_locks[key]);
  b->refcnt++;
  release(&bcache.bufmap_locks[key]);
}

void
bunpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno);
  acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;
  release(&bcache.bufmap_locks[key]);
}


