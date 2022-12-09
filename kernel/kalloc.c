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
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  uint8 ref_count[PHYSTOP >> PGSHIFT];
} page_ref_count;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&page_ref_count.lock, "pgref");
  memset(page_ref_count.ref_count, 0, PHYSTOP >> PGSHIFT);
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  struct run *r;

  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    memset(p, 1, PGSIZE);
    r = (struct run *)p;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&page_ref_count.lock);
  if (--page_ref_count.ref_count[(uint64)pa >> PGSHIFT] != 0) {
    release(&page_ref_count.lock);
    return;
  }
  release(&page_ref_count.lock);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r) {
    kmem.freelist = r->next;
    acquire(&page_ref_count.lock);
    page_ref_count.ref_count[(uint64)r >> PGSHIFT] = 1;
    release(&page_ref_count.lock);
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

void
inc_refcount(uint64 pa) {
  acquire(&page_ref_count.lock);
  page_ref_count.ref_count[pa >> PGSHIFT]++;
  release(&page_ref_count.lock);
}

/* Do the following steps atomically:
 *  1. if page's reference count is 1(i.e., not shared), return true. 
 *  2. else, decrease page's reference count, return false.
 */
int
dec_refcount_if_shared(uint64 pa) {
  acquire(&page_ref_count.lock);
  uint8 ret = page_ref_count.ref_count[pa >> PGSHIFT];
  if (ret != 1) {
    --page_ref_count.ref_count[pa >> PGSHIFT];
    ret = 0;
  } else {
    ret = 1;
  }
  release(&page_ref_count.lock);
  return ret;
}
