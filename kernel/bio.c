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

#define NBUF_BUCKETS 13

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  struct spinlock locks[NBUF_BUCKETS];
  struct buf *bufs[NBUF_BUCKETS];
} bcache;

uint bucket_num(uint blockno) {
        return (blockno) % NBUF_BUCKETS;
}

void remove_buf(struct buf *b) {
    struct buf **bucket = &bcache.bufs[bucket_num(b->blockno)]; 
    if (*bucket == b) {
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

void
binit(void)
{
    struct buf *b;
    memset(&bcache, 0, sizeof(bcache));
    initlock(&bcache.lock, "bcache");
    for (int i = 0; i < NBUF_BUCKETS; i++) {
        initlock(&bcache.locks[i],"bcache.bucket");
    }
    for(b = bcache.buf; b < bcache.buf+NBUF; b++){
        initsleeplock(&b->lock, "buffer");
        b->blockno = 0;
        insert_buf(b, bucket_num(b->blockno));
    }
}

struct buf* get_lru(struct buf* b) {
    struct buf* lru = 0; 
    uint least = 0xFFFFFFFF;    
    for (; b != 0; b = b->next) {
        if ((b->refcnt == 0) && (b->timestamp < least)) {
            lru = b;
            least = b->timestamp; 
        } 
    }
    return lru;
}

// You must maintain the invariant that at most one copy of each block is cached.
// How am I maintaining this invariant?
// By ensuring that there's only one copy of a given block in it's corresponding hash table bucket.

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
// Searching in the hash table for a buffer and allocating an entry for that buffer when the buffer is not found must be atomic.
    
static struct buf*
bget(uint dev, uint blockno)
{
    struct buf *b;

    // Is the block already cached?
    uint bucket = bucket_num(blockno);
    acquire(&bcache.locks[bucket]);
    for(b = bcache.bufs[bucket]; b != 0; b = b->next){
        if(b->dev == dev && b->blockno == blockno){
            b->refcnt++;
            release(&bcache.locks[bucket]);
            acquiresleep(&b->lock);
            return b;
        }
    }

    b = get_lru(bcache.bufs[bucket]);
    if (b) {
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        release(&bcache.locks[bucket]); 
        acquiresleep(&b->lock);
        return b; 
    }
    // Not cached. Check the other buckets for a free block.
    for(int i = 1; i < NBUF_BUCKETS; i++){
        uint next_bucket = (bucket + i) % NBUF_BUCKETS;
        acquire(&bcache.locks[next_bucket]);
        b = get_lru(bcache.bufs[next_bucket]);
        if (b) {
            remove_buf(b);
            insert_buf(b, bucket);
            b->dev = dev;
            b->blockno = blockno;
            b->valid = 0;
            b->refcnt = 1;
            release(&bcache.locks[next_bucket]); 
            release(&bcache.locks[bucket]); 
            acquiresleep(&b->lock);
            return b; 
        }
        release(&bcache.locks[next_bucket]);
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
    uint bucket = bucket_num(b->blockno);

    acquire(&bcache.locks[bucket]);
    acquire(&tickslock);
    b->timestamp = ticks;
    release(&tickslock);
    b->refcnt--;
    release(&bcache.locks[bucket]);
}

void
bpin(struct buf *b) {
    uint bucket = bucket_num(b->blockno);
    acquire(&bcache.locks[bucket]);
    b->refcnt++;
    release(&bcache.locks[bucket]);
}

void
bunpin(struct buf *b) {
    uint bucket = bucket_num(b->blockno);
    acquire(&bcache.locks[bucket]);
    b->refcnt--;
    release(&bcache.locks[bucket]);
}
