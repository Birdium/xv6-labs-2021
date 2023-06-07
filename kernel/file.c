//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"
#include "fcntl.h"

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE){
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

// Write to file f.
// addr is a user virtual address.
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r != n1){
        // error from writei
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }

  return ret;
}


uint64 mmap(uint64 addr, int length, int prot, int flags, struct file *f, int offset) {
  if (addr) 
    panic("mmap: addr != 0");
  if (offset)
    panic("mmap: offset != 0");
  if (!f->readable && (prot & PROT_READ))
    return -1;
  if (!f->writable && (prot & PROT_WRITE) && !(flags & MAP_PRIVATE))
    return -1;

  struct proc* p = myproc();
  length = PGROUNDUP(length);
  for (int i = 0; i < VMA_NUM; i++) {
    if (p->vma[i].valid == 0) {
      p->vma[i] = (struct vma){
        .valid = 1,
        .addr = p->sz, 
        .f = f,
        .length = length,
        .prot = prot,
        .flags = flags,
        .offset = offset
      };
      filedup(f);
      p->sz += length;
      // printf("mmap: [%p, %p), %d\n", (void*)(p->sz - length), (void*)(p->sz), prot);
      return p->vma[i].addr;
    }
  }
  return 0;
}

int munmap(uint64 addr, int length) {
  struct proc *p = myproc();
  struct vma *v = 0;
  addr = PGROUNDDOWN(addr);
  length = PGROUNDUP(length);
  for (int i = 0; i < VMA_NUM; i++) {    
    #define IN_RANGE(x, start, len) ((start <= x && x < start + len)) 
    if (p->vma[i].valid && IN_RANGE(addr, p->vma[i].addr, p->vma[i].length)
                        && IN_RANGE(addr + length, p->vma[i].addr, p->vma[i].length)) {
      v = &p->vma[i];
    }
  }
  if (!v) {
    return 0;
  }
  // printf("%p [%p, %p)\n", v, (void*)v->addr, (void*)(v->addr + v->length));
  if (v->addr == addr) {
    v->addr += length;
    v->length -= length;
    if (v->flags & MAP_SHARED) 
      filewrite(v->f, addr, length);
    uvmunmap(p->pagetable, addr, length / PGSIZE, 1);
    if (v->length == 0) {
      fileclose(v->f);
      v->valid = 0;
    }
  }  
  return 0;
}
