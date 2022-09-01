// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock locks[NCPU];
  struct run *freelists[NCPU];
} kmem;

void
kinit()
{
 for (int i = 0; i < NCPU; i++) 
     initlock(&kmem.locks[i], "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  push_off();
  int id = cpuid();
  pop_off();
  acquire(&kmem.locks[id]);
  struct run *freeList = kmem.freelists[id]; 
  r->next = freeList;
  kmem.freelists[id] = r;
  release(&kmem.locks[id]);
}


struct run *
steal_page() {
    struct run* r = 0;
    for (int i = 0; i < NCPU; i++) {
        acquire(&kmem.locks[i]);
        r = kmem.freelists[i];
        if (r)
            kmem.freelists[i] = r->next;
        release(&kmem.locks[i]); 
        if (r) 
            break;
    } 
    return r;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  push_off();
  int id = cpuid();
  pop_off();

  acquire(&kmem.locks[id]);
  r = kmem.freelists[id];
  if(r) {
    kmem.freelists[id] = r->next;
    release(&kmem.locks[id]);
  } 
  else {
   release(&kmem.locks[id]);
   r = steal_page();
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
