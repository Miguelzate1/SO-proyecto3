#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if (t == SBRK_EAGER || n < 0) {
    if (growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if (addr + n < addr)
      return -1;
    if (addr + n > USYSCALL) //se cambio trapfame por usyscall
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if (n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (killed(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep_prepare(&ticks);
    release(&tickslock);
    sleep();
    acquire(&tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
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


uint64
sys_pgaccess(void)
{
  uint64 base;
  int npages;
  uint64 maskaddr;
  uint64 mask = 0;
  struct proc *p = myproc();

  // 1. Leer argumentos (en tu xv6 devuelven void)
  argaddr(0, &base);
  argint(1, &npages);
  argaddr(2, &maskaddr);

  // 2. Validar rango
  if(npages < 0 || npages > 64)
    return -1;

  // 3. Revisar cada página solicitada
  for(int i = 0; i < npages; i++){
    uint64 va = base + (uint64)i * PGSIZE;
    pte_t *pte = walk(p->pagetable, va, 0);

    if(pte != 0 && (*pte & PTE_V) && (*pte & PTE_A)){
      mask |= (1L << i);
      *pte &= ~PTE_A;
    }
  }

  // 4. Copiar al usuario (pasando p->sz como exige tu versión)
  if(copyout(p->pagetable, p->sz, maskaddr, (char *)&mask, sizeof(mask)) < 0)
    return -1;

  return 0;
}

uint64
sys_vmprint(void)
{
  vmprint(myproc()->pagetable);
  return 0;
}