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
} kmem[NCPU]; //为每个CPU分配独立的freelist，多个CPU并发分配物理内存不会相互竞争

char *kmem_lock_names[] = {
    "keme_cpu_0",
    "keme_cpu_1",
    "keme_cpu_2",
    "keme_cpu_3",
    "keme_cpu_4",
    "keme_cpu_5",
    "keme_cpu_6",
    "keme_cpu_7",
};


void
kinit()
{
  for(int i = 0; i < NCPU; ++i){
    initlock(&kmem[i].lock, kmem_lock_names[i]);
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

  push_off(); //关闭cpu中断

  int cpu = cpuid();//获取cpuid，关闭cpu中断调用cpuid才安全，中断可能导致后续执行的cpuid改变

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
  struct run *r;
  
  push_off();

  int cpu = cpuid();

  acquire(&kmem[cpu].lock);

  r = kmem[cpu].freelist;  //去除一个物理页，页表项本身就是物理页
  //当前cpu已经没有freelist时，去其他cpu中偷取内存页
  if(!r){
    release(&kmem[cpu].lock);
    int steal_page = 64; //指定偷64个内存页
    struct run *steal_list = 0;
    for(int i = 0; i < NCPU; i++){
      if(i == cpu) 
        continue;
      
      acquire(&kmem[i].lock);
      if(!kmem[i].freelist){
        release(&kmem[i].lock);
        continue;
      }
      /*
      struct run* rr = kmem[i].freelist;

      //循环将kmem[i]的freelist移动到kmem[cpu]中，链表的插入

      */
      struct run* rr = kmem[i].freelist;
      while(rr && steal_page){
        kmem[i].freelist = rr->next;//icpu的链表等于自身下一个节点
        rr->next = steal_list;
        //kmem[cpu].freelist = rr;//将当前页赋给当前cpu
        steal_list = rr;//指向i的下一个节点
        rr = kmem[i].freelist;
        steal_page--;
        
      }

      release(&kmem[i].lock);
      if(steal_page == 0)
        break;
    }
    if(steal_list != 0){
        r = steal_list;
        acquire(&kmem[cpu].lock);
        kmem[cpu].freelist = r->next;
        release(&kmem[cpu].lock);
    }
  }else{
    kmem[cpu].freelist = r->next;
    release(&kmem[cpu].lock);
  }
  /*
    r = kmem[cpu].freelist;

  if(r)
    kmem[cpu].freelist = r->next;

  release(&kmem[cpu].lock);
  */
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
