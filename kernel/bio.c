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

#define NBUCKETS 13
struct {
  struct spinlock global_lock; //全局锁，仅用于空闲块获取
  struct spinlock lock[NBUCKETS];//每个哈希桶一个锁
  struct buf buf[NBUF];
  struct buf hashbucket[NBUCKETS]; //每个哈希桶一个双向链表头结点
} bcache;

int hash(int blockno) {
  return blockno % NBUCKETS;
}

void
binit(void)
{
  //初始化所有哈希桶的锁以及全局锁
  struct buf *b;
  for(int i = 0; i < NBUCKETS; i++){
    initlock(&bcache.lock[i], "bcache");
  }
  initlock(&bcache.global_lock,"bcache_global");
  

  // 初始化所有哈希桶的头结点
  for(int i = 0; i < NBUCKETS; i++){
    bcache.hashbucket[i].prev = &bcache.hashbucket[i];
    bcache.hashbucket[i].next = &bcache.hashbucket[i];
  }

  int key;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    key = hash(b->blockno);
    b->next = bcache.hashbucket[key].next;
    b->prev = &bcache.hashbucket[key];
    initsleeplock(&b->lock, "buffer");
    bcache.hashbucket[key].next->prev = b;
    bcache.hashbucket[key].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int key = hash(blockno);
  acquire(&bcache.lock[key]);

  // 先在块号对应的哈希桶中找
  for(b = bcache.hashbucket[key].next; b != &bcache.hashbucket[key]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[key]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.lock[key]);

  // 如果没有命中，则需要遍历哈希桶，找空闲块
  // 经分析每个哈希桶并行遍历应该更快，但是懒得写
  // 应当先获取全局锁，然后获取每个hash块的锁
  acquire(&bcache.global_lock);
  // 优先遍历当前哈希桶，由于锁被释放，要先看一下是否能够命中
  acquire(&bcache.lock[key]);
  for(b = bcache.hashbucket[key].next; b != &bcache.hashbucket[key]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[key]);
      release(&bcache.global_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  for(b = bcache.hashbucket[key].prev; b != &bcache.hashbucket[key]; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock[key]);
      release(&bcache.global_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // 如果上面两次遍历都没成功，则找其他桶
  for(int i = 0; i < NBUCKETS; i++){
    if(i == key) continue;
    acquire(&bcache.lock[i]);
    for(b = bcache.hashbucket[i].prev; b != &bcache.hashbucket[i]; b = b->prev){
      if(b->refcnt == 0){
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        //从当前哈希桶中移除，并放入正确的哈希桶
        b->next->prev = b->prev;
        b->prev->next = b->next;
        b->next = bcache.hashbucket[key].next;
        b->prev = &bcache.hashbucket[key];
        bcache.hashbucket[key].next->prev = b;
        bcache.hashbucket[key].next = b;
        //按顺序释放锁
        release(&bcache.lock[i]);
        release(&bcache.lock[key]);
        release(&bcache.global_lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.lock[i]);
  }
  
  panic("bget: no buffers");
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

  int key = hash(b->blockno);
  acquire(&bcache.lock[key]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // 放入对应桶的头节点
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.hashbucket[key].next;
    b->prev = &bcache.hashbucket[key];
    bcache.hashbucket[key].next->prev = b;
    bcache.hashbucket[key].next = b;
  }
  
  release(&bcache.lock[key]);
}

void
bpin(struct buf *b) {
  int key = hash(b->blockno);
  acquire(&bcache.lock[key]);
  b->refcnt++;
  release(&bcache.lock[key]);
}

void
bunpin(struct buf *b) {
  int key = hash(b->blockno);
  acquire(&bcache.lock[key]);
  b->refcnt--;
  release(&bcache.lock[key]);
}


