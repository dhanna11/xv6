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

uint bucket_num(uint blockno) {
        return (blockno) % NBUF_BUCKETS;
}

void print_buckets();
void remove_buf(struct buf *b) {
    struct buf **bucket = &bcache.bufs[bucket_num(b->blockno)]; 
    struct buf *head =  bcache.bufs[bucket_num(b->blockno)]; 
    if (head == b) {
       *bucket = b->next;
       b->next = 0;
       return;
    }
    for (struct buf* t = *bucket; t != 0; t  = t->next) {
        if (t->next == b) {
           t->next = t->next->next;
           b->next = 0;
           return;
        }
    
    }
    panic("should not get here");

}

void insert_buf(struct buf *b, uint bucket) {
    b->next = bcache.bufs[bucket];
    bcache.bufs[bucket] = b;
}


void print_buckets() {
    printf("printing buckets\n");
    for (int bucket = 0; bucket < NBUF_BUCKETS; bucket++) {
        printf("bucket #%d \n", bucket);
        for (struct buf* b = bcache.bufs[bucket]; b != 0; b = b->next) {
            printf("b->dev %d b->blockno %d b->valid %d\n", b->dev, b->blockno, b->valid);
        }
    }
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");
  
  for (int i = 0; i < NBUF_BUCKETS; i++) {
    initlock(&bcache.locks[i],"bcache.bucket");
  } 
  int blockno = 0; 
  for(b = bcache.buf; b < bcache.buf+NBUF; b++, blockno++){
    initsleeplock(&b->lock, "buffer");
    b->blockno = blockno; 
     uint bucket = bucket_num(blockno);
     
    insert_buf(b, bucket);
  }
  print_buckets();
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
    struct buf *b;
    
    acquire(&bcache.lock);
   
    uint bucket = bucket_num(blockno);
    //acquire(&bcache.locks[bucket]);
    for(b = bcache.bufs[bucket]; b != 0; b = b->next){
        if(b->dev == dev && b->blockno == blockno){
            b->refcnt++;
            acquire(&tickslock);
            b->timestamp = ticks;
            release(&tickslock);
     //       release(&bcache.locks[bucket]);
            release(&bcache.lock);
            acquiresleep(&b->lock);
            return b;
        }
    }
    //release(&bcache.locks[bucket]);
    // Not cached.
    // Loop through all possible bufs for a free buf. Make sure we move buf from old bucket to new bucket.
    for(b = bcache.buf; b < bcache.buf+NBUF; b++){
        if(b->refcnt == 0) {
            break;
        }
    }
    if (b->refcnt != 0)
        panic("bget: no buffers");
    uint old_bucket = bucket_num(b->blockno);
    uint new_bucket = bucket_num(blockno);
    
    if (old_bucket != new_bucket) {
     //   acquire(&bcache.locks[old_bucket]);
        remove_buf(b); 
     //   release(&bcache.locks[old_bucket]);
    //    acquire(&bcache.locks[new_bucket]);
        insert_buf(b,new_bucket); 
     //   release(&bcache.locks[new_bucket]);
    } 
    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;
    acquire(&tickslock);
    b->timestamp = ticks;
    release(&tickslock);
    print_buckets();
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

  acquire(&bcache.lock); 
  releasesleep(&b->lock);
  
  
//  uint bucket = bucket_num(b->blockno); 
//  acquire(&bcache.locks[bucket]);
  b->refcnt--;
  
  acquire(&tickslock);
  b->timestamp = ticks;
  release(&tickslock);
  
 // release(&bcache.locks[bucket]);
  release(&bcache.lock);
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


