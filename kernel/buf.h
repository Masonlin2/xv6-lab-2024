struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;// 表示现在有多少buf正在使用当前这个缓冲区
  // struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
  uint last_used;
};

