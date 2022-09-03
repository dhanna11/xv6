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

#define NBUF_BUCKETS 1
struct {
  struct spinlock lock;
  struct spinlock locks[NBUF_BUCKETS];
  struct buf *bufs[NBUF_BUCKETS];
  struct buf buf[NBUF];
} bcache;

void remove_buf(struct buf *b) {
    if (b->next)  
        b->next->prev = b->prev;
    if (b->prev)
        b->prev->next = b->next; 
}

void insert_buf(struct buf *b, struct buf **bucket, struct buf *head) {
    if (head){
        head->prev = b;
    }
    b->next = head;
    b->prev = 0; 
    *bucket = b;
}

uint bucket_num(uint blockno) {
    return (blockno+3) % NBUF_BUCKETS;
}

void
binit(void)
{
  struct buf *b;

  memset(&bcache, 0, sizeof(bcache)); 
  initlock(&bcache.lock, "bcache");
  
  for (int i = 0; i < NBUF_BUCKETS; i++) {
    initlock(&bcache.locks[i],"bcache.bucket");
  } 
  int blockno = 0; 
  for(b = bcache.buf; b < bcache.buf+NBUF; b++, blockno++){
    initsleeplock(&b->lock, "buffer");
    uint bucket = bucket_num(blockno); 
    insert_buf(b, &bcache.bufs[bucket], bcache.bufs[bucket]);
  }

}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
    struct buf *b;
    struct buf* lru = 0;
    uint bucket = bucket_num(blockno);
    acquire(&bcache.lock);
    acquire(&bcache.locks[bucket]);
    for(b = bcache.bufs[bucket]; b != 0; b = b->next){
        if(b->dev == dev && b->blockno == blockno){
            b->refcnt++;
            acquire(&tickslock);
            b->timestamp = ticks;
            release(&tickslock);
            release(&bcache.locks[bucket]);
            release(&bcache.lock);
            acquiresleep(&b->lock);
            return b;
        }
    }
    release(&bcache.locks[bucket]);

    // Not cached.
    // Loop through all possible bufs for a free buf. Make sure we move buf from old bucket to new bucket.
    for(b = bcache.buf; b < bcache.buf+NBUF; b++){
        if(b->refcnt == 0) {
            if ((!lru) || (b->timestamp < lru->timestamp))
                lru = b;

        }
    }
    if (!lru)
        panic("bget: no buffers");
    b = lru;
    uint old_bucket = bucket_num(b->blockno);
    uint new_bucket = bucket_num(blockno);
    
    if (old_bucket != new_bucket) {
        acquire(&bcache.locks[old_bucket]);
        remove_buf(b); 
        release(&bcache.locks[old_bucket]);
        acquire(&bcache.locks[new_bucket]);
        insert_buf(b, &bcache.bufs[new_bucket], bcache.bufs[new_bucket]); 
        release(&bcache.locks[new_bucket]);
    } 
    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;
    acquire(&tickslock);
    b->timestamp = ticks;
    release(&tickslock);
    release(&bcache.lock);
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
 
  uint bucket = bucket_num(b->blockno); 
  acquire(&bcache.lock); 
  acquire(&bcache.locks[bucket]);
  releasesleep(&b->lock);
  b->refcnt--;
  
  acquire(&tickslock);
  b->timestamp = ticks;
  release(&tickslock);
  release(&bcache.lock); 
  release(&bcache.locks[bucket]);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}


