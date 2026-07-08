/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  Blocks are never coalesced or reused.  The size of
 * a block is found at the first aligned word before the block (we need
 * it for realloc).
 *
 * This code is correct and blazingly fast, but very bad usage-wise since
 * it never frees anything.
 */
 /**
  这个项目中，一开始会申请一大块内存，称为堆(Heap),也就是后面所有的操作都是基于这一开始就拿到的一片内存。
  一个概念澄清:
  栈 = 自动管理的那片内存(函数局部变量住的地方)。
  堆 = 需要手动管理、要自己申请自己释放的那片内存。
  所以我们叫这个内存“沙盒内存”。但是这不代表这个内存是假的，你所有的操作其实都是真的在操作内存的，只不过你操作的内存都局限在
  一开始申请的这一篇内存中而已。也就是说哪怕崩了，给这一片内存释放了就行了。

  然后这个项目里还有一个前置声明，就是说它保证了它传给你的 PTR 指针一定是合法的原装指针，就是它一定是指向一个合法位置，
  比如说某一个 block 的开头。不会随便给你一个指向 block 中间位置的指针，所以说这种判断就不用去做了。 
  */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"


#define DEBUG // 用来触发 #ifdef， 如果注释掉那就不会触发
#ifdef DEBUG // ifdef就是 if defined
# define dbg_printf(...) printf(__VA_ARGS__) // 如果defined，那么所有的dbg_printf运行起来的效果就等同于printf，__VA_ARGS__就是把参数原封不动的传下去
#else
# define dbg_printf(...) // 如果没有defined，那么所有的dbg_printf的效果就等同于啥都没有(可以看到左边dbg_printf后面没跟东西)
#endif


/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

/* single word (4) or double word (8) alignment */
/**
  这里ALIGNMENT的作用和机器的位数无关(32或者64)，这里ALIGNMENT是因为某些类型的数据必须存放在8的倍数的地址上(0,8,16...)。
  如果你放在5这种地址上，CPU读取的时候要么直接崩溃，要么很慢。
 */
#define ALIGNMENT 8 // 以后凡是出现ALIGNMENT, 替换成8

/* rounds up to the nearest multiple of ALIGNMENT */
/**
  这里其实和之前的有一点区别，但是本质上还是替换，只不过这边是动态替换
  可以看到这里定义了一个参数size, 所以其实很好理解，比如代码中的ALIGN(5)会被替换成（5+（8-1))然后和0x7的取反做合
  这个效果就是:
  1. 先+7会让当前这个数向上凑近到最近的8的倍数
  2. 和0x7的取反做合，也就是和二进制111111..11000(32长度)做合，那么效果就是把这个数整除8后会有的余数都扔掉

  所以5+8-1=12
  12&(~7) = 8
 */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

// 这里需要说明一下，这个sizeof是C的内置函数，作用就是返回“这个类型/这个变量”会在内存中占多少个字节
// 然后这里需要讲一下size_t这个类型，它是C内置的一个类型，它的宽度是随着机器走的，在64位机器上是8字节，在32位机器上是4字节，所以这个类型是专门用来表示宽度的
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

// 这一个项目不会真的去动你的内存，而是通过一大个“堆”来模拟内存
/** 
  下面这个函数，想要理解，需要知道前提，和它下面的malloc函数有关系
  可以发现malloc在申请内存的时候，会多申请8个字节的内存空间(这里是8是因为这是一个64位机器，2^8=64，正好是这个机器
  的最大宽度。而且8个字节的这个数能保证一定可以记录该机器上任何大小的内存空间。这8个字节的空间不是给你申请下来用来存放东西的，
  而是用来记录它后面跟着的那一片内存是多大。所以实际长这样子: |8字节长度记录|----实际的"内存"----|
                                                                ⬆️
                                                                p是这条分界线
  然后下面这个SIZE_PTR拿到p之后会减去SIZE_T_SIZE，这个SIZE_T_SIZE我们上面讲过了，它返回你当前机器的最宽的变量所占用的字节。
*/
#define SIZE_PTR(p)  ((size_t*)(((char*)(p)) - SIZE_T_SIZE))

/**
  这上面的这些 #define 啥的都是预处理指令，它们在编译前就会被处理。
  整体的链路: .c源码 -> [预处理] -> 展开后的源码 -> [真正编译] -> .o文件 -> 链接 -> 可执行
*/

/*
 * mm_init - Called when a new trace starts.
 */
int mm_init(void)
{
  return 0;
}

/*
 * malloc - Allocate a block by incrementing the brk pointer.
 *      Always allocate a block whose size is a multiple of the alignment.
 */
void *malloc(size_t size)
{
  int newsize = ALIGN(size + SIZE_T_SIZE);
  unsigned char *p = mem_sbrk(newsize);
  if (p == (void *)-1)
    return NULL;
  else {
    p += SIZE_T_SIZE;
    *SIZE_PTR(p) = size;
    return p;
  }
}

/*
 * free - We don't know how to free a block.  So we ignore this call.
 *      Computers have big memories; surely it won't be a problem.
 */
void free(void *ptr){
	/*Get gcc to be quiet */
	(void)ptr;

}

/*
 * realloc - Change the size of the block by mallocing a new block,
 *      copying its data, and freeing the old block.  I'm too lazy
 *      to do better.
 */
void *realloc(void *oldptr, size_t size)
{
  size_t oldsize;
  void *newptr;

  /* If size == 0 then this is just free, and we return NULL. */
  if(size == 0) {
    free(oldptr);
    return 0;
  }

  /* If oldptr is NULL, then this is just malloc. */
  if(oldptr == NULL) {
    return malloc(size);
  }

  newptr = malloc(size);

  /* If realloc() fails the original block is left untouched  */
  if(!newptr) {
    return 0;
  }

  /* Copy the old data. */
  oldsize = *SIZE_PTR(oldptr);
  if(size < oldsize) oldsize = size;
  memcpy(newptr, oldptr, oldsize);

  /* Free the old block. */
  free(oldptr);

  return newptr;
}

/*
 * calloc - Allocate the block and set it to zero.
 */
void *calloc (size_t nmemb, size_t size)
{
  size_t bytes = nmemb * size;
  void *newptr;

  newptr = malloc(bytes);
  memset(newptr, 0, bytes);


  return newptr;
}

/*
 * mm_checkheap - There are no bugs in my code, so I don't need to check,
 *      so nah!
 */
void mm_checkheap(int verbose){
	/*Get gcc to be quiet. */
	(void)verbose;
}
