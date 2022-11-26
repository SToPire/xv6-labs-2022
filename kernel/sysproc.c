#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  const int MAX_PAGE = 512;
  uint64 base;
  int len;
  uint64 mask;
  struct proc *p;
  pte_t *pte;
  uint8 tmpmask[MAX_PAGE / 8];

  argaddr(0, &base);
  argint(1, &len);
  argaddr(2, &mask);

  base = PGROUNDDOWN(base);
  if (len > MAX_PAGE) {
    return -1;
  }

  memset(tmpmask, 0, MAX_PAGE / 8);

  p = myproc();

  for (int i = 0; i < len; i++) {
    if ((pte = walk(p->pagetable, base + i * PGSIZE, 0))) {
      if (*pte & PTE_A) {
        *pte = *pte & ~PTE_A;
        tmpmask[i / 8] |= (1 << (i % 8));
      }
    }
  }

  copyout(p->pagetable, mask, (char *)tmpmask, (len + 7) / 8);

  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
