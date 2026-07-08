#include <unistd.h>

void mem_init(void);  // 这个不会用到             
void mem_deinit(void);  // 这个也不会用到

// 这里函数名前面带*意思就是这个函数返回的是一个指针
void *mem_sbrk(int incr); // 拓宽内存incr个字节, incr>0, 返回指向新的内存空间的第一个字节的指针
void mem_reset_brk(void); // 这个也不会用到
void *mem_heap_lo(void); // 返回指向整个内存的第一个字节的指针
void *mem_heap_hi(void); // 返回指向整个内存的最后一个字节的指针，注意最后一个字节的位置是会随着内存被扩大而变化的
size_t mem_heapsize(void); // 返回当前整个内存的大小(字节)

/**
  什么是Page?
  操作系统不是一个字节一个字节管理内存的，而是把内存分成很多块(也就是Page)，每一块可能包含几个KB(通常是4KB)，然后总共有几百万个Page。
  有程序请求空间时就分出去一个Page。
  所以这里这个函数返回的就是当前操作系统的一个Page的大小。
 */
size_t mem_pagesize(void); 

