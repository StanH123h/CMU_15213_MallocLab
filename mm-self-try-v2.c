#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

#include "mm.h"
#include "memlib.h"

/**
    Results for this version:
    Results for mm malloc:
  valid  util   ops    secs     Kops  trace
   yes     0%  100000  0.010235  9770 ./traces/alaska.rep
 * yes    23%    4805  0.003810  1261 ./traces/amptjp.rep
 * yes    28%    4162  0.000515  8080 ./traces/bash.rep
 * yes    56%   57716  1.347066    43 ./traces/boat.rep
 * yes    19%    5032  0.003649  1379 ./traces/cccp.rep
 * yes    31%   11991  0.001643  7298 ./traces/chrome.rep
 * yes     2%   20000  0.322799    62 ./traces/coalesce-big.rep
   yes    50%   14400  0.000094152542 ./traces/coalescing-bal.rep
   yes   100%      15  0.000003  5357 ./traces/corners.rep
 * yes    30%    5683  0.004904  1159 ./traces/cp-decl.rep
 u yes     1%      --        --    -- ./traces/exhaust.rep
 * yes    29%    8000  0.001072  7464 ./traces/firefox.rep
   yes    60%   99804  0.011258  8865 ./traces/firefox-reddit.rep
   yes    67%     118  0.000027  4453 ./traces/hostname.rep
 * yes    63%   19405  0.002134  9095 ./traces/login.rep
 * yes    25%     200  0.000022  9009 ./traces/lrucd.rep
   yes    75%     372  0.000048  7686 ./traces/ls.rep
   yes    90%      10  0.000002  5000 ./traces/malloc.rep
   yes    82%      17  0.000001 14167 ./traces/malloc-free.rep
 *  no      -       -         -     - ./traces/needle.rep
 * yes    35%     200  0.000033  6024 ./traces/nlydf.rep
   yes    68%    1494  0.000200  7451 ./traces/perl.rep
 * yes    33%     200  0.000022  9091 ./traces/qyqyc.rep
 * yes    37%    4800  0.001531  3135 ./traces/random.rep
 * yes    37%    4800  0.001477  3249 ./traces/random2.rep
 * yes    83%     147  0.000028  5231 ./traces/rm.rep
 * yes    32%     200  0.000024  8403 ./traces/rulsr.rep
 p yes     --    6495  0.013909   467 ./traces/seglist.rep
   yes   100%      12  0.000004  3000 ./traces/short2.rep
            -         -     -
 */

/**
    基于version 1加入了比较基本的空间释放机制。
    依靠末尾1个字节的释放Flag来标记被释放的空间。
    每次malloc都O(1)遍历目前已用内存中是否有可以再复用的空间。
 */


// #define DEBUG // 用来触发 #ifdef， 如果注释掉那就不会触发
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

// 返回一个指向ptr指针后面最近的一个为8的倍数的地址的第一个字节的指针
#define ALIGN_PTR(ptr) ((void *)(((uintptr_t)(ptr) + 7) & ~(uintptr_t)7))
// 返回指针ptr往前数8个位置的一个指针(也就是记录长度的区块的开头)
#define SIZE_PTR(ptr) ((size_t *)((char *)(ptr) - 8))
// 返回指向代表ptr指向的区块的释放状态的字节的指针, ptr是指向该区块内容第一个字节的指针
#define FREE_STATUS_PTR(ptr, size) (((char *)(ptr))+size)

// 用于记录现在到底有没有已经占用的内存，配合find_available_space使用防止访问还没申请的内存从而导致seg_fault
static bool has_memory = false;

/*
 * mm_init - 每个新 trace 开始前被调用,做初始化工作。
 *           返回 0 表示成功,-1 表示出错。
 *           注意:不要在这里调用 mem_init。
 */
int mm_init(void)
{
    dbg_printf("\n==========================INIT-start===\n");
    dbg_printf("mem_heap_hi=%p\n", mem_heap_hi());
    dbg_printf("mem_heap_low=%p\n", mem_heap_lo());
    dbg_printf("\n============================INIT-end===\n\n");
    has_memory = false;
    return 0;
}

// 用于查找是否有已经释放的空间并且存的下新的数据，传入的参数就是要存的新数据的大小
// 如果找到了，返回指向那块空间的数据区块的第一个字节的指针
static char *find_available_space(size_t size)
{
    if (!has_memory) {
        return (char *) -1;
    }

    // 从开头开始找
    size_t *find_size_ptr = mem_heap_lo();
    // 注意这边的+1实际上是加了8个字节
    signed char *find_availability_ptr = (char *)(find_size_ptr + 1) + *find_size_ptr;
    char *next_addr = find_size_ptr;

    while (mem_heap_hi() >= find_availability_ptr) {
        dbg_printf("avai_ptr=%p\n", find_availability_ptr);
        dbg_printf("size_ptr=%p\n", find_size_ptr);
        dbg_printf("size=%zu\n", *find_size_ptr);
        if ((*find_size_ptr >= size) && (*find_availability_ptr == 0)) {
            return (char *)find_size_ptr;
        } else {
            char *next_addr = find_size_ptr;
            next_addr += *find_size_ptr;
            next_addr += 9;
            find_size_ptr = ALIGN_PTR(next_addr);
            find_availability_ptr = (char *)(find_size_ptr + 1) + *find_size_ptr;
        } 
    }
    return (char *) -1;
}

/*
 * malloc - 分配一块至少 size 字节的内存,返回指向它的指针。
 *          返回的地址必须对齐。失败返回 NULL。
 */
void *malloc(size_t size)
{
    dbg_printf("========malloc()========\n");
    // void * 隐式转换成 char *
    char *new_mem_start = ALIGN_PTR((char *)mem_heap_hi() + 1);
    dbg_printf("1: mem_heap_hi=%p\n", mem_heap_hi());
    dbg_printf("2. new_mem_start=%p\n", new_mem_start);
    int diff = (int)(((uintptr_t)new_mem_start - (uintptr_t)mem_heap_hi()) - 1);
    dbg_printf("3. diff=%zu\n", diff);

    // 这里要+9了 因为第一个+8留给size_ptr存大小 第二个+1留给free_status存释放状态
    size_t new_size = size + diff + 9;
    dbg_printf("4. size = %zu, new_size = %zu\n", size, new_size);

    char *empty_space_ptr = find_available_space(size);
    if (empty_space_ptr != (signed char *) -1) {
        new_mem_start = empty_space_ptr;
    } else {
        void *ret = mem_sbrk(size + diff + 8 + 1);
        if (ret == ((void *) -1)) {
            return NULL;
        }
    }
    dbg_printf("5-1. size_ptr=%p\n", new_mem_start);
    has_memory = true;
    
    size_t *size_ptr = new_mem_start;
    *size_ptr = size;
    new_mem_start += 8;

    // 最后一位用来存放释放状态，0代表可用，-1代表不可用
    char *free_status_ptr = new_mem_start + size;
    dbg_printf("5-2. avai_ptr=%p\n", free_status_ptr);
    // 用全1占位表示已占用
    *free_status_ptr = 0xFF;
    dbg_printf("5-3. new_mem_start=%p\n", new_mem_start);
    dbg_printf("6: new_mem_heap_hi=%p\n", mem_heap_hi());
    dbg_printf("========malloc()========\n\n");
    return (void *) new_mem_start;
}

/*
 * free - 释放 ptr 指向的块。
 *        ptr 保证是之前 malloc/calloc/realloc 返回的原装指针。
 *        free(NULL) 什么都不做。
 */
void free(void *ptr)
{
    if ((ptr == NULL) || !(has_memory && ptr < mem_heap_hi())) return;
    size_t *size_ptr = SIZE_PTR(ptr);
    char *free_status = FREE_STATUS_PTR((char *)ptr, *size_ptr);
    // dbg_printf("\naaaa\n");
    // dbg_printf("mem_heap_hi=%p\n", mem_heap_hi());
    // dbg_printf("ptr = %p\n", ptr);
    *free_status = 0;
    // dbg_printf("didnt crash!\n");
}

/*
 * realloc - 把 oldptr 指向的块改成 size 大小,返回新块指针。
 *           size==0 等价于 free;oldptr==NULL 等价于 malloc。
 *           原有数据要保留(拷贝到新块,拷贝量为新旧大小的较小值)。
 */
void *realloc(void *oldptr, size_t size)
{
    dbg_printf("========realloc()========\n");
    if (oldptr == NULL) {
        dbg_printf("========realloc()==Calling Malloc======>\n");
        return malloc(size);
    } else if (size == 0) {
        free(oldptr);
        dbg_printf("========realloc()===NULL=====\n\n");
        return NULL;
    }  else {
        dbg_printf("===entering realloc else===\n");
        void *dest = malloc(size);
        if (dest == NULL) {
            return NULL;
        }
        size_t old_size = *SIZE_PTR(oldptr);
        dbg_printf("dest=%p, oldptr=%p, old_size=%zu \n", dest, oldptr, old_size);
        memcpy(dest, oldptr, old_size);
        free(oldptr);
        dbg_printf("========realloc()===returning dest=====\n\n");
        return dest;
    }
}

/*
 * calloc - 分配 nmemb 个、每个 size 字节的内存,并清零。
 */
void *calloc(size_t nmemb, size_t size)
{
    dbg_printf("========calloc()========\n");
    // 这里未来要加验证，验证最终需要申请的内存是否足够
    size_t final_size = nmemb * size;
    void *ret = malloc(final_size);
    if (ret == NULL) {
        // 其实这里return ret效果也一样的
        return NULL;
    }
    memset(ret, 0, size);
    return ret;
}

/*
 * mm_checkheap - 堆一致性检查(debug 用)。现在可以先留空。
 */
void mm_checkheap(int verbose)
{
    // 先空着，未来要做检查
}