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

#define NBUCKET 13

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
} bcache;

struct {
  struct buf *head;
  struct spinlock lock;
  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
} buckets[NBUCKET];

int H(int n) {
  return n % NBUCKET;
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock, "buffer");
  }

  for (int i = 0; i < NBUCKET; i++) {
    initlock(&buckets[i].lock, "bcache.bucket");
    buckets[i].head = 0;
  }

  // Create linked list of buffers
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    int h = H(b->blockno);
    b->timestamp = ticks;
    b->blockno = h;
    b->next = buckets[h].head;
    buckets[h].head = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int h = H(blockno);

  acquire(&buckets[h].lock);

  // Is the block already cached?
  for(b = buckets[h].head; b != 0; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&buckets[h].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // find a block in current hash table
  struct buf *lb = 0;

  for(b = buckets[h].head; b != 0; b = b->next){
    if(b->refcnt == 0 && b->blockno == blockno){
      if (!lb || b->timestamp < lb->timestamp) {
        lb = b;
      }
    }
  }
  
  if (lb) {
    b = lb;
    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;
    release(&buckets[h].lock);
    acquiresleep(&b->lock);
    return b;
  }

  // acquire(&bcache.lock);

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for (int i = (h + 1) % NBUCKET; i != h; i = (i + 1) % NBUCKET) {
    acquire(&buckets[i].lock);
    struct buf *lb = 0;
    for(b = buckets[i].head; b != 0; b = b->next){
      if(b->refcnt == 0) {
        if (!lb || b->timestamp < lb->timestamp) {
          lb = b;
        }
      }
    }
    if (lb) {
      b = lb;
      struct buf *prev = buckets[i].head;
      if (prev == b) {
        buckets[i].head = b->next;
      }
      else {
        while (prev && prev->next != b) {
          prev = prev->next;
        }
        prev->next = b->next;
      }

      b->next = buckets[h].head;
      buckets[h].head = b;

      release(&buckets[i].lock);
      // release(&bcache.lock);
      b->timestamp = ticks;
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;

      release(&buckets[h].lock);
      acquiresleep(&b->lock);
      return b;
    }
    release(&buckets[i].lock);
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

  int h = H(b->blockno);

  acquire(&buckets[h].lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->timestamp = ticks;
    // no one is waiting for it.
  }
  
  release(&buckets[h].lock);
}

void
bpin(struct buf *b) {
  int h = H(b->blockno);
  acquire(&buckets[h].lock);
  b->refcnt++;
  release(&buckets[h].lock);
}

void
bunpin(struct buf *b) {
  int h = H(b->blockno);
  acquire(&buckets[h].lock);
  b->refcnt--;
  release(&buckets[h].lock);
}


