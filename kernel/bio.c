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

#define NBUKETS 13

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  //struct buf head;

  struct buf buckets[NBUKETS];
  struct spinlock bucketslock[NBUKETS];
} bcache;

int 
getHash(int blockno){
  return blockno % NBUKETS;
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  for(int i = 0; i < NBUKETS; i++){
    initlock(&bcache.bucketslock[i], "bcache");
    bcache.buckets[i].prev = &bcache.buckets[i];
    bcache.buckets[i].next = &bcache.buckets[i];
  }

  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    int hash = getHash(b->blockno);
    b->next = bcache.buckets[hash].next;
    b->prev = &bcache.buckets[hash];
    initsleeplock(&b->lock, "buffer");
    bcache.buckets[hash].next->prev = b;
    bcache.buckets[hash].next = b;
  }

  // Create linked list of buffers
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  // for(b = bcache.buf; b < bcache.buf+NBUF; b++){
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   initsleeplock(&b->lock, "buffer");
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  int hash = getHash(blockno);
  struct buf *b;

  //acquire(&bcache.lock);
  acquire(&bcache.bucketslock[hash]);

  // Is the block already cached?
  for(b = bcache.buckets[hash].next; b != &bcache.buckets[hash]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      //release(&bcache.lock);
      release(&bcache.bucketslock[hash]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  // for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
  //   if(b->refcnt == 0) {
  //     b->dev = dev;
  //     b->blockno = blockno;
  //     b->valid = 0;
  //     b->refcnt = 1;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }

  for(b = bcache.buckets[hash].prev; b != &bcache.buckets[hash]; b = b->prev){
    if(b->refcnt == 0){
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.bucketslock[hash]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.bucketslock[hash]);

  for(int i = (hash + 1) % NBUKETS; i != hash; i = (i + 1) % NBUKETS){
    if(i < hash){
      acquire(&bcache.bucketslock[i]);
      acquire(&bcache.bucketslock[hash]);
    }
    else{
      acquire(&bcache.bucketslock[hash]);
      acquire(&bcache.bucketslock[i]);
    }
    

    for(b = bcache.buckets[i].prev; b != &bcache.buckets[i]; b = b->prev){
      if(b->refcnt == 0){
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;

        b->next->prev = b->prev;
        b->prev->next = b->next;

        b->next = bcache.buckets[hash].next;
        b->prev = &bcache.buckets[hash];
        bcache.buckets[hash].next->prev = b;
        bcache.buckets[hash].next = b;

        if(i < hash){
          release(&bcache.bucketslock[hash]);
          release(&bcache.bucketslock[i]);
        }
        else{
          release(&bcache.bucketslock[i]);
          release(&bcache.bucketslock[hash]);
        }
        
        acquiresleep(&b->lock);
        return b;
      }
    }

    if(i < hash){
      release(&bcache.bucketslock[hash]);
      release(&bcache.bucketslock[i]);
    }
    else{
      release(&bcache.bucketslock[i]);
      release(&bcache.bucketslock[hash]);
    }
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
  int hash = getHash(b->blockno);

  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  //acquire(&bcache.lock);
  acquire(&bcache.bucketslock[hash]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.buckets[hash].next;
    b->prev = &bcache.buckets[hash];
    bcache.buckets[hash].next->prev = b;
    bcache.buckets[hash].next = b;
  }
  
  //release(&bcache.lock);
  release(&bcache.bucketslock[hash]);
}

void
bpin(struct buf *b) {
  int hash = getHash(b->blockno);
  //acquire(&bcache.lock);
  acquire(&bcache.bucketslock[hash]);
  b->refcnt++;
  //release(&bcache.lock);
  release(&bcache.bucketslock[hash]);
}

void
bunpin(struct buf *b) {
  int hash = getHash(b->blockno);
  //acquire(&bcache.lock);
  acquire(&bcache.bucketslock[hash]);
  b->refcnt--;
  //release(&bcache.lock);
  release(&bcache.bucketslock[hash]);
}


