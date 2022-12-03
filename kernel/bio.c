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

#define NBUCKET (13)

struct {
  struct spinlock lock[NBUCKET];
  struct buf* head[NBUCKET];
} bcache;

uint bhash(uint dev, uint blockno) {
  return (dev + blockno) % NBUCKET;
}

void
binit(void)
{
  struct buf *b;
  for (int i = 0; i < NBUCKET; i++) {
    initlock(&bcache.lock[i], "bcache");

    b = kalloc();
    if (!b) {
      panic("bget: no memory");
    }

    // Create linked list of buffers
    bcache.head[i] = b;
    bcache.head[i]->prev = bcache.head[i];
    bcache.head[i]->next = bcache.head[i];
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  uint hv = bhash(dev, blockno);

  acquire(&bcache.lock[hv]);

  // Is the block already cached?
  for(b = bcache.head[hv]->next; b != bcache.head[hv]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[hv]);
      acquiresleep(&b->lock);
      return b;
    }
  }

 /* Allocate a new page as buffer */
  b = kalloc();
  if (!b) {
    panic("bget: no memory");
  }

  b->dev = dev;
  b->blockno = blockno;
  b->valid = 0;
  b->refcnt = 1;
  initsleeplock(&b->lock, "buffer");

  b->prev = bcache.head[hv]->prev;
  b->next = bcache.head[hv];

  bcache.head[hv]->prev->next = b;
  bcache.head[hv]->prev = b;

  release(&bcache.lock[hv]);
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
  int hv = bhash(b->dev, b->blockno);

  releasesleep(&b->lock);

  acquire(&bcache.lock[hv]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;

  #ifdef LAB_LOCK
    freelock(&b->lock.lk);
  #endif
    kfree(b);
  }
  
  release(&bcache.lock[hv]);
}

void
bpin(struct buf *b) {
  uint hv = bhash(b->dev, b->blockno);
  acquire(&bcache.lock[hv]);
  b->refcnt++;
  release(&bcache.lock[hv]);
}

void
bunpin(struct buf *b) {
  uint hv = bhash(b->dev, b->blockno);
  acquire(&bcache.lock[hv]);
  b->refcnt--;
  release(&bcache.lock[hv]);
}


