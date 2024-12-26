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
//哈希表中桶的索引号
#define NBUFMAP_BUCKET 13

#define BUFMAP_HASH(dev, blockno) ((((dev) << 27) | (blockno))%NBUFMAP_BUCKET)

struct {
  //哈希表
  struct spinlock bufmaplock[NBUFMAP_BUCKET];//桶锁
  struct buf bufmap[NBUFMAP_BUCKET];
  
  //
  struct buf buf[NBUF];
  struct spinlock eviction_lock;
  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.

  //struct buf head;
} bcache;

void
binit(void)
{
  //struct buf *b;
  for(int i = 0; i < NBUFMAP_BUCKET; i++){
    initlock(&bcache.bufmaplock[i], "bcache_bufmap");
    bcache.bufmap[i].next = 0;
  }

  // Create linked list of buffers
  //更改，不使用双向链表存储，转用哈希表存储
  for(int i = 0; i < NBUF; i++){//NBUF,缓存区块数量
    //初始化缓存区块
    struct buf* b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer");
    b->lastuse = 0;
    b->refcnt = 0;//

    //将系统的所有缓存添加到bufmap[0]
    b->next = bcache.bufmap[0].next;
    bcache.bufmap[0].next = b;
  }
  /*
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  */
  initlock(&bcache.eviction_lock, "bcache_eviction");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  //从哈希表获取桶号
  uint key = BUFMAP_HASH(dev, blockno);

  acquire(&bcache.bufmaplock[key]);

  // Is the block already cached?blockno缓存区块是否已经在缓存中
  for(b = bcache.bufmap[key].next; b; b = b->next){
    //在缓存区中
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;//
      release(&bcache.bufmaplock[key]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  //驱逐锁（大锁）本质：并行化查找缓存区块是否已经在缓存中（桶），在缓存区中就不上大锁，若是不在（类似于发生冲突），需要上大锁执行下列复杂操作（乐观锁思想）
  //不在缓存区
  //防止死锁，先释放当前桶锁
  release(&bcache.bufmaplock[key]);
  //防止blockno的缓存区块被重复创建，加上驱逐锁
  acquire(&bcache.eviction_lock);

  //释放桶锁和驱逐锁之间可能使其他CPU创建blockno缓存区块，故复检
  for(b = bcache.bufmap[key].next; b; b = b->next){
    //在缓存区中
    if(b->dev == dev && b->blockno == blockno){
      //添加引用计数时加上桶锁
      acquire(&bcache.bufmaplock[key]);//限制其他CPU对桶的操作（遍历查询不在大锁范围内）
      b->refcnt++;//
      release(&bcache.bufmaplock[key]);
      release(&bcache.eviction_lock);//释放驱逐锁//允许竞态条件发生，但是允许进行检测
      acquiresleep(&b->lock);
      return b;
    }
  }

  //仍然不存在缓存区，此时只有驱逐锁，查看桶中的LRU-buf
  struct buf* before_least = 0;
  uint holding_bucket = -1;
  
  for(int i = 0; i < NBUFMAP_BUCKET; ++i){
    acquire(&bcache.bufmaplock[i]);

    int newfound = 0;
    for(b = &bcache.bufmap[i]; b->next; b = b->next){
      if(b->next->refcnt == 0 && (!before_least || b->next->lastuse < before_least->next->lastuse)){
        before_least = b;//找到最符合的节点
        newfound = 1;
      } 
    }
    //如果没有找到新的LRU-buf，就释放当前桶锁
    if(!newfound)
      release(&bcache.bufmaplock[i]);
    else{
      if(holding_bucket != -1)//
        release(&bcache.bufmaplock[holding_bucket]);
      holding_bucket = i;//自身当前锁没有释放，直到找到下一个更加符合的才释放，防止该桶被其他CPU操作
    }

  }
  //如果没有找到任何一个LRU-buf
  if(!before_least)
    panic("bget: no buffers");
  
  b = before_least->next;// b = LRU-buf

  if(holding_bucket != key){//想要偷的块不在key桶，需要将块从它的桶里面驱逐出来，加入到key桶
    before_least->next = b->next;
    release(&bcache.bufmaplock[holding_bucket]);//因为最新的一个没有释放

    //将LRU-buf添加到key桶
    acquire(&bcache.bufmaplock[key]);
    b->next = bcache.bufmap[key].next;//
    bcache.bufmap[key].next = b;//
  }
  //设置新的
  b->dev = dev;
  b->blockno = blockno;
  b->refcnt = 1;
  b->valid = 0;
  //释放相关锁
  release(&bcache.bufmaplock[key]);
  release(&bcache.eviction_lock);

  acquiresleep(&b->lock);
  return b;
  /*
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}
  */
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

  uint key = BUFMAP_HASH(b->dev, b->blockno);

  acquire(&bcache.bufmaplock[key]);
  b->refcnt--;
  /*
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  */
  if(b->refcnt == 0){
    b->lastuse = ticks;//时间标记
  }
  
  release(&bcache.bufmaplock[key]);
}

void
bpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno);
  acquire(&bcache.bufmaplock[key]);
  b->refcnt++;
  release(&bcache.bufmaplock[key]);
}

void
bunpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno);

  acquire(&bcache.bufmaplock[key]);
  b->refcnt--;
  release(&bcache.bufmaplock[key]);
}


