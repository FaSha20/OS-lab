#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "fs.h"
#include "sleeplock.h"
#include "stat.h"
#include "buf.h"
#include "file.h"
#include "date.h"

#define CYCLE_AGE_LIMIT 8000
#define INFINITY 9999999

struct
{
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);
int random(int max);
void sem_sleep(struct proc* p);
void sem_wakeup(struct proc* p);

void pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int cpuid()
{
  return mycpu() - cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu *
mycpu(void)
{
  int apicid, i;

  if (readeflags() & FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i)
  {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc *
myproc(void)
{
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

// retrun the current time
int getTime(void)
{
  uint ticks0;
  acquire(&tickslock);
  ticks0 = ticks;
  release(&tickslock);
  return ticks0;
}

// PAGEBREAK: 32
//  Look in the process table for an UNUSED proc.
//  If found, change state to EMBRYO and initialize
//  state required to run in the kernel.
//  Otherwise return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->proc_level = 2;
  p->arrival_time = getTime();
  p->cycles = 1;
  p->p_ratio = 1;
  p->t_ratio = 1;
  p->c_ratio = 1;
  p->rank = INFINITY;
  p->last_cpu_time = 0;
  p->wait_cycles = 0;

  release(&ptable.lock);

  // Allocate kernel stack.
  if ((p->kstack = kalloc()) == 0)
  {
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe *)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint *)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context *)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

// PAGEBREAK: 32
//  Set up first user process.
void userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  initproc = p;
  if ((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0; // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if (n > 0)
  {
    if ((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  else if (n < 0)
  {
    if ((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy process state from proc.
  if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0)
  {
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for (i = 0; i < NOFILE; i++)
    if (curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void)
{

  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if (curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for (fd = 0; fd < NOFILE; fd++)
  {
    if (curproc->ofile[fd])
    {
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->parent == curproc)
    {
      p->parent = initproc;
      if (p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->parent != curproc)
        continue;
      havekids = 1;
      if (p->state == ZOMBIE)
      {
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || curproc->killed)
    {
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock); // DOC: wait-sleep
  }
}

struct proc* round_robin_finder(void)
{
  struct proc *p;
  struct proc *best = 0;

  int now = ticks;
  int max_proc = -100000;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE || p->proc_level != 1)
        continue;
      if(now - p->last_cpu_time > max_proc){
        max_proc = now - p->last_cpu_time;
        best = p;
      }
  }
  return best;
}

struct proc *lottery_finder(void)
{
  struct proc *p;
  struct proc *ps[NPROC];
  int limits[NPROC];
  int i = 0;
  int sum = 0;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == RUNNABLE && p->proc_level == 2)
    {
      ps[i] = p;
      sum += p->n_tickets;
      limits[i] = sum;
      i++;
    }
  int rnum = random(sum);
  //cprintf("rand: %d\n", rnum);
  for (int j = 0; j < i; j++)
  {
    if (rnum < limits[j])
      return ps[j];
  }
  return 0;
}

struct proc *bjf_finder(void)
{
  struct proc *p;
  int best_rank = INFINITY;
  struct proc* best_proc;  
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == RUNNABLE && p->proc_level == 3) {
      p ->rank = (p->p_ratio * 3) + (p->t_ratio * p->arrival_time) + (p->c_ratio * p->cycles);
      if(p->rank < best_rank) {
        best_rank = p->rank;
        best_proc = p;
      }
    }
  }
  if(best_rank != INFINITY) {
    
    return best_proc;
  }
  return 0;
}

int find_process(struct proc* q)
{
  struct proc *p, *best = 0;

  // Round Robin
  int now = ticks;
  int max_proc = -100000;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state == RUNNABLE && p->proc_level == 1)
        if(now - p->last_cpu_time > max_proc){
          max_proc = now - p->last_cpu_time;
          best = p;
        }
  }
  if(best != 0){
    q = best;
    return 0;
  }

  // Lottery
  struct proc *ps[NPROC];
  int limits[NPROC];
  int i = 0;
  int sum = 0;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == RUNNABLE && p->proc_level == 2)
    {
      ps[i] = p;
      sum += p->n_tickets;
      limits[i] = sum;
      i++;
    }
  int rnum = random(sum);
  for (int j = 0; j < i; j++)
  {
    if (rnum < limits[j]){
      q = ps[j];
      /*->c_ratio = j;
      q->t_ratio = rnum;
      q->p_ratio = limits[j];*/
      return 0;
    }
  }
  
  //BJF
  int best_rank = INFINITY;
  struct proc* best_proc;  
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == RUNNABLE && p->proc_level == 3) {
      p ->rank = (p->p_ratio * 3) + (p->t_ratio * p->arrival_time) + (p->c_ratio * p->cycles);
      if(p->rank < best_rank) {
        best_rank = p->rank;
        best_proc = p;
      }
    }
  }
  if(best_rank != INFINITY) {
    q = best_proc;
    q->rank = best_rank;
    return 0;
  }
 
  return -1;
}

// PAGEBREAK: 42
//  Per-CPU process scheduler.
//  Each CPU calls scheduler() after setting itself up.
//  Scheduler never returns.  It loops, doing:
//   - choose a process to run
//   - swtch to start running that process
//   - eventually that process transfers control
//       via swtch back to the scheduler.

void scheduler(void)
{
  struct proc *p = 0;
  struct proc *q;
  struct cpu *c = mycpu();
  c->proc = 0;
  for (;;)
  {
    // Enable interrupts on this processor.
    sti();
    acquire(&ptable.lock);
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->state != RUNNABLE)
        continue;
      // Aging
      for (q = ptable.proc; q < &ptable.proc[NPROC]; q++)
      {
        if (q->wait_cycles >= CYCLE_AGE_LIMIT && q->state == RUNNABLE)
        {
          q->wait_cycles = 0;
          q->proc_level = 1;
        }
        else if (q->state == RUNNABLE && q != p)
        {
          q->wait_cycles++;
        }
      }

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.

      
      find_process(p);
      c->proc = p;
      
      switchuvm(p);
      
      p->state = RUNNING;
      p->wait_cycles = 0;
      p->cycles += 1;
      swtch(&(c->scheduler), p->context);
      
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
  }
}


// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&ptable.lock))
    panic("sched ptable.lock");
  if (mycpu()->ncli != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (readeflags() & FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  acquire(&ptable.lock); // DOC: yieldlock
  myproc()->state = RUNNABLE;
  myproc()->last_cpu_time = ticks;
  myproc()->cycles += 0.1;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first)
  {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if (p == 0)
    panic("sleep");

  if (lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if (lk != &ptable.lock)
  {                        // DOC: sleeplock0
    acquire(&ptable.lock); // DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if (lk != &ptable.lock)
  { // DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

// PAGEBREAK!
//  Wake up all processes sleeping on chan.
//  The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->killed = 1;
      // Wake process from sleep if necessary.
      if (p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

// PAGEBREAK: 36
//  Print a process listing to console.  For debugging.
//  Runs when user types ^P on console.
//  No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [EMBRYO] "embryo",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if (p->state == SLEEPING)
    {
      getcallerpcs((uint *)p->context->ebp + 2, pc);
      for (i = 0; i < 10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

// return the largest prime factor of input number
int largest_prime_factor(int n)
{
  long int div = 2, maxFact;
  while (n != 0)
  {
    if (n % div != 0)
      div = div + 1;
    else
    {
      maxFact = n;
      n = n / div;
      if (n == 1)
      {
        return maxFact;
      }
    }
  }
  return 0;
}

// find processes that call a system call
void find_callers(int n, int **procs)
{
  int j = 0;
  int f = 0;
  int temp[64] = {0};
  acquire(&ptable.lock);

  for (int i = 0; i < sizeof(ptable.proc) / sizeof(ptable.proc[0]); i++)
  {
    if (ptable.proc[i].systemcalls[n] == 1)
    {
      temp[j] = ptable.proc[i].pid;
      j++;
    }
  }
  *procs = temp;
  for (int i = 0; i < j; i++)
  {
    f = 1;
    cprintf("%d", temp[i]);
    if (i != j - 1)
    {
      cprintf(", ");
    }
  }
  if (f == 0)
    cprintf("No process has called this system call");
  cprintf("\n");

  release(&ptable.lock);
}

//----------------

int change_queue(int pid, int level)
{
  struct proc *p;
  if (level < 1 || level > 3)
    return -1;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->proc_level = level;
      return 0;
    }
  }
  return -1;
}

int set_tickets(int pid, int count)
{
  struct proc *p;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->n_tickets = count;
      return 0;
    }
  }
  return -1;
}

int sys_set_bjf_params(int p_ratio, int t_ratio, int c_ratio)
{
  struct proc *p;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    p->p_ratio = p_ratio;
    p->t_ratio = t_ratio;
    p->c_ratio = c_ratio;
  }
  return 0;
}

int proc_set_bjf_params(int pid,int p_ratio, int t_ratio, int c_ratio)
{
  struct proc *p;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->p_ratio = p_ratio;
      p->t_ratio = t_ratio;
      p->c_ratio = c_ratio;
      return 0;
    }
  }
  return -1;
}

int print_process(void)
{
  struct proc *p;
  acquire(&ptable.lock);
  cprintf("name\t\tpid\tstate\t\tqueue_level\tcycle\ttickets\tarrival\trank\tp_ratio\tt_ratio\tc_ratio\n");
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == 0)
      continue;
    cprintf("%s\t\t", p->name);    
    cprintf("%d\t", p->pid);
    switch (p->state)
    {
    case UNUSED:
      cprintf("%s\t\t", "UNUSED");
      break;
    case EMBRYO:
      cprintf("%s\t\t", "EMBRYO");
      break;
    case SLEEPING:
      cprintf("%s\t", "SLEEPING");
      break;
    case RUNNABLE:
      cprintf("%s\t", "RUNNABLE");
      break;
    case RUNNING:
      cprintf("%s\t\t", "RUNNING");
      break;
    case ZOMBIE:
      cprintf("%s\t\t", "ZOMBIE");
      break;

    default:
      break;
    }

    cprintf("%d\t\t", p->proc_level);
    cprintf("%d\t", p->cycles);
    cprintf("%d\t", p->n_tickets);
    cprintf("%d\t", p->arrival_time);
    cprintf("%d\t", p->rank);
    cprintf("%d\t", p->p_ratio);
    cprintf("%d\t", p->t_ratio);
    cprintf("%d\t\n", p->c_ratio);
  }
  release(&ptable.lock);
  return -1;
}

int random(int max)
{

  if (max <= 0)
  {
    return 1;
  }

  static int z1 = 12345; // 12345 for rest of zx
  static int z2 = 12345; // 12345 for rest of zx
  static int z3 = 12345; // 12345 for rest of zx
  static int z4 = 12345; // 12345 for rest of zx

  int b;
  b = (((z1 << 6) ^ z1) >> 13);
  z1 = (((z1 & 4294967294) << 18) ^ b);
  b = (((z2 << 2) ^ z2) >> 27);
  z2 = (((z2 & 4294967288) << 2) ^ b);
  b = (((z3 << 13) ^ z3) >> 21);
  z3 = (((z3 & 4294967280) << 7) ^ b);
  b = (((z4 << 3) ^ z4) >> 12);
  z4 = (((z4 & 4294967168) << 13) ^ b);

  // if we have an argument, then we can use it
  int rand = ((z1 ^ z2 ^ z3 ^ z4)) % max;

  if (rand < 0)
  {
    rand = rand * -1;
  }

  return rand;
}

//------------------------------------------------

#define MAXPROC 100

typedef struct
{
    int value;
    struct proc* list[MAXPROC];
    int last;
}semaphore;

semaphore sems[6];

void sem_sleep(struct proc *p1)
{
  acquire(&ptable.lock); 
  p1->state = SLEEPING;
  sched();
  release(&ptable.lock);
}

void sem_wakeup(struct proc *p1)
{
  acquire(&ptable.lock);
  p1->state = RUNNABLE;
  release(&ptable.lock);
}

int sem_init(int i , int v)
{
  sems[i].value = v;
  sems[i].last = 0;
  return 0;
}

int sem_acquire(int i)
{
  if(sems[i].value <= 0)
  {
    struct proc* p = myproc();
    sems[i].list[sems[i].last ++] = p;
    sem_sleep(p);
  }
  else
    sems[i].value--;
  if(i != 5)
    cprintf("Philosopher %d ACQUIRED chopstick %d ,with value %d\n",
           myproc()->pid-3 , i, sems[i].value);
  else cprintf("Philosopher %d is READY to eat, with value %d\n",
           myproc()->pid-3 , sems[i].value);

  return 0;
}

int sem_release(int i)
{
  if(sems[i].last)
  {
    struct proc* p = sems[i].list[--sems[i].last];
    sem_wakeup(p);
  }
  else
    sems[i].value++;
  
  if(i != 5)
    cprintf("Philosopher %d RELEASED chopstick %d ,with value %d\n",
           myproc()->pid-3 , i, sems[i].value);
  else 
    cprintf("Philosopher %d STOP eating, with value %d\n",
           myproc()->pid-3, sems[i].value);
  return 0;
}
