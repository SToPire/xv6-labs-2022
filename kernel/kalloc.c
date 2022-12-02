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
} kmem[NCPU];

void
kinit()
{
  for (int i = 0; i < NCPU; i++) {
    initlock(&kmem[i].lock, "kmem");
  }
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

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  int cpu;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  /* Use cpuid() only when interrupt is off. */
  push_off();
  cpu = cpuid();
  acquire(&kmem[cpu].lock);

  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;

  release(&kmem[cpu].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r, *new_head;
  int cpu;
  int cur_size;
  int retry = 0;
  const int STEAL_SIZE = 1024;

beg:
  /* local allocation */
  push_off();
  cpu = cpuid();
  acquire(&kmem[cpu].lock);

  r = kmem[cpu].freelist;
  if (r) kmem[cpu].freelist = r->next;

  release(&kmem[cpu].lock);
  pop_off();

  /* local allocation fails, try to steal other's page */
  if (!r) {
    for (int i = 0; i < NCPU; i++) {
      /* take victim's lock */
      acquire(&kmem[i].lock);
      cpu = cpuid();

      /* victim shouldn't be myself */
      if (i == cpu) {
        release(&kmem[i].lock);
        continue;
      }

      /* victim is also poor, find next victim */
      r = kmem[i].freelist;
      if (!r) {
        release(&kmem[i].lock);
        continue;
      }

      /* try to fetch 1024 pages, or as much as possible */
      new_head = r;
      cur_size = 1;
      while (r->next && cur_size != STEAL_SIZE) {
        r = r->next;
        ++cur_size;
      }

      /* cut stolen pages off from victim's list */
      kmem[i].freelist = r->next;
      r->next = (void *)0;

      /* Take my lock to insert stolen pages.
       * DO NOT worry about deadlock.
       * I have no free page so nobody may steal from me, thus not take my lock.
       */
      acquire(&kmem[cpu].lock);
      r = new_head;
      kmem[cpu].freelist = r->next;
      release(&kmem[cpu].lock);

      release(&kmem[i].lock);

      break;
    }
  }

  if (r) {
    memset((char *)r, 5, PGSIZE); // fill with junk
    return (void *)r;
  } else if (!retry) {
    /* retry one more time when failure */
    retry = 1;
    goto beg;
  }

  /* really failure */
  return (void *)0;
}
