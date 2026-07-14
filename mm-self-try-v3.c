/**
    Results for this version:
    Results for mm malloc:
  valid  util   ops    secs     Kops  trace
   yes    30%  100000  0.006176 16192 ./traces/alaska.rep
 * yes    99%    4805  0.004275  1124 ./traces/amptjp.rep
 * yes    77%    4162  0.002708  1537 ./traces/bash.rep
 * yes    56%   57716  1.253972    46 ./traces/boat.rep
 * yes    99%    5032  0.003743  1344 ./traces/cccp.rep
 * yes    67%   11991  0.026069   460 ./traces/chrome.rep
 * yes    99%   20000  0.001304 15339 ./traces/coalesce-big.rep
   yes     0%   14400  0.001610  8946 ./traces/coalescing-bal.rep
   yes   100%      15  0.000003  5556 ./traces/corners.rep
 * yes    99%    5683  0.006062   938 ./traces/cp-decl.rep
 u yes    54%      --        --    -- ./traces/exhaust.rep
 * yes    68%    8000  0.011030   725 ./traces/firefox.rep
   yes    77%   99804  2.495633    40 ./traces/firefox-reddit.rep
   yes    85%     118  0.000030  3933 ./traces/hostname.rep
 * yes    88%   19405  0.150471   129 ./traces/login.rep
 * yes    82%     200  0.000012 17094 ./traces/lrucd.rep
   yes    90%     372  0.000087  4256 ./traces/ls.rep
   yes    90%      10  0.000003  4000 ./traces/malloc.rep
   yes    85%      17  0.000001 15455 ./traces/malloc-free.rep
 u yes    99%      --        --    -- ./traces/needle.rep
 * yes    91%     200  0.000020 10204 ./traces/nlydf.rep
   yes    83%    1494  0.001164  1284 ./traces/perl.rep
 * yes    85%     200  0.000016 12500 ./traces/qyqyc.rep
 * yes    94%    4800  0.004367  1099 ./traces/random.rep
 * yes    92%    4800  0.004006  1198 ./traces/random2.rep
 * yes    92%     147  0.000036  4027 ./traces/rm.rep
 * yes    95%     200  0.000022  9050 ./traces/rulsr.rep
 p yes     --    6495  0.014822   438 ./traces/seglist.rep
   yes   100%      12  0.000005  2553 ./traces/short2.rep
18 17     85%  153836  1.482933   104

Perf index = 48 (util) & 0 (thru) = 48/100
 */

 /**
    这个版本算是第一个比较完整的版本。加入了更完善的内存复用机制。
    实现了大内存空间被复用时的切片机制。
  */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "mm.h"
#include "memlib.h"


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
#define MIN(a, b) ((a) < (b) ? (a) : (b))

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

static void merge_free_chunks(size_t *first_size_ptr, size_t *second_size_ptr) {
    dbg_printf("First ptr before merge: %p\n", first_size_ptr);
    dbg_printf("Second ptr before merge: %p\n", second_size_ptr);
    dbg_printf("First ptr size before merge: %zu\n", *first_size_ptr);
    dbg_printf("Second ptr size before merge: %zu\n", *second_size_ptr);
    *first_size_ptr = (((uintptr_t)second_size_ptr - (uintptr_t)first_size_ptr) + *second_size_ptr);
    dbg_printf("First ptr size after merge: %zu\n", *first_size_ptr);
    dbg_printf("Second ptr size after merge: %zu\n", *second_size_ptr);
    // mm_checkheap(0);
}

// 用于查找是否有已经释放的空间并且存的下新的数据，传入的参数就是要存的新数据的大小
// 如果找到了，返回指向那块空间的大小区块的第一个字节的指针
static char *find_available_space(size_t size)
{
    if (!has_memory) {
        return (char *) -1;
    }

    // 从开头开始找
    size_t min_size = SIZE_MAX;
    char *min_size_ptr = (char *) -1;
    size_t *find_size_ptr = mem_heap_lo();
    // 注意这边的+1实际上是加了8个字节
    signed char *find_availability_ptr = NULL;
    char *next_addr = find_size_ptr;
    size_t satisfy_threshold = size + size / 2;
    char *last_availability_ptr = NULL;
    size_t *last_size_ptr = NULL;
    while (mem_heap_hi() > find_size_ptr) {
        find_availability_ptr = (char *)(find_size_ptr + 1) + *find_size_ptr;
        dbg_printf("avai_ptr=%p\n", find_availability_ptr);
        dbg_printf("size_ptr=%p\n", find_size_ptr);
        dbg_printf("target_size=%zu\n", size);
        dbg_printf("min_size=%zu\n", min_size);
        dbg_printf("mem_heap_hi=%p\n", mem_heap_hi());
        dbg_printf("chunk_size=%zu\n", *find_size_ptr);
        if (*find_availability_ptr == 0) {

            if (last_availability_ptr != NULL && *last_availability_ptr == 0) {
                merge_free_chunks(last_size_ptr, find_size_ptr);

                // 必须把指针移动到合并完的区块的头部
                find_size_ptr = last_size_ptr;
                last_availability_ptr = find_availability_ptr;
            } else if ((*find_size_ptr >= size) && (*find_size_ptr < min_size)) {
                min_size_ptr = (char *)find_size_ptr;
                min_size = *find_size_ptr;
                if (min_size <= satisfy_threshold) {
                    return min_size_ptr;
                }
            }

            if (!(last_availability_ptr != NULL && *last_availability_ptr == 0)) {
                last_size_ptr = find_size_ptr;
                last_availability_ptr = find_availability_ptr;
            } 
            
            // if (last_availability_ptr != NULL && *last_availability_ptr == 0) {
            //     merge_free_chunks(last_size_ptr, find_size_ptr);

            //     // 必须把指针移动到合并完的区块的头部
            //     find_size_ptr = last_size_ptr;
            //     last_availability_ptr = find_availability_ptr;
            // } else {
            //     last_size_ptr = find_size_ptr;
            //     last_availability_ptr = find_availability_ptr;
            // }
        } else {
            last_size_ptr = find_size_ptr;
            last_availability_ptr = find_availability_ptr;
        }
        char *next_addr = find_size_ptr;
        next_addr += *find_size_ptr;
        next_addr += 9;
        dbg_printf("next size ptr: %p\n", next_addr);
        dbg_printf("next Addr=%p\n", next_addr);
        // printf("Last size ptr: %p\n", last_size_ptr);
        // printf("Curr size ptr: %p\n", find_size_ptr);
        find_size_ptr = ALIGN_PTR(next_addr);
        
    }
    // mm_checkheap(0);
    return min_size_ptr;
}

// 用于把一个大chunk分成一个size大小的chunk+剩余, 注意size_ptr指向的地址上存放的size应该是大chunk的size
static void split_free_chunk(size_t *size_ptr, size_t size) {
    size_t whole_size = *size_ptr;

    // 先拆第一个chunk出来
    *size_ptr = size;
    char *ptr = (char *)size_ptr;
    size_t *first_size_ptr = ptr;
    ptr += (8 + size);

    // 标记成空
    *ptr = 0;

    char *aligned_ptr = ALIGN_PTR(ptr + 1);
    uintptr_t diff = (uintptr_t) aligned_ptr - (uintptr_t) ptr - 1;
    size_t first_chunk_size = size + 9;
    if (whole_size > ((first_chunk_size + diff) + 9)) {
        // 然后标记第二个chunk
        size_ptr = aligned_ptr;
        *size_ptr = (whole_size - size - 9 - diff);
        aligned_ptr += (8 + *size_ptr);
        *aligned_ptr = 0;
    } else {
        *first_size_ptr = whole_size;
    }
    // mm_checkheap(0);
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

    size_t *size_ptr = new_mem_start;
    char *empty_space_ptr = find_available_space(size);
    if (empty_space_ptr != (signed char *) -1) {
        new_mem_start = empty_space_ptr;
        split_free_chunk(new_mem_start, size);
        size_ptr = new_mem_start;
    } else {
        void *ret = mem_sbrk(size + diff + 8 + 1);
        if (ret == ((void *) -1)) {
            return NULL;
        }
        *size_ptr = size;
    }
    dbg_printf("5-1. size_ptr=%p\n", new_mem_start);
    has_memory = true;
    
    new_mem_start += 8;

    // 最后一位用来存放释放状态，0代表可用，-1代表不可用
    char *free_status_ptr = new_mem_start + *size_ptr;
    dbg_printf("5-2. avai_ptr=%p\n", free_status_ptr);
    // 用全1占位表示已占用
    *free_status_ptr = 0xFF;
    dbg_printf("5-3. new_mem_start=%p\n", new_mem_start);
    dbg_printf("6: new_mem_heap_hi=%p\n", mem_heap_hi());
    dbg_printf("========malloc()========\n\n");
    // mm_checkheap(0);
    return (void *) new_mem_start;
}

/*
 * free - 释放 ptr 指向的块。
 *        ptr 保证是之前 malloc/calloc/realloc 返回的原装指针。
 *        free(NULL) 什么都不做。
 */
void free(void *ptr)
{
    // printf("start free\n");
    if ((ptr == NULL) || !(has_memory && ptr < mem_heap_hi())) return;
    size_t *size_ptr = SIZE_PTR(ptr);
    char *free_status_ptr = FREE_STATUS_PTR((char *)ptr, *size_ptr);
    // dbg_printf("\naaaa\n");
    // dbg_printf("mem_heap_hi=%p\n", mem_heap_hi());
    // dbg_printf("ptr = %p\n", ptr);
    // printf("ptr = %p\n", ptr);
    // printf("size_ptr = %p\n", size_ptr);
    // printf("size = %zu\n", *size_ptr);
    // printf("free_status_ptr = %p\n", free_status_ptr);
    *free_status_ptr = 0;
    // dbg_printf("didnt crash!\n");
    // printf("end free\n");
    // mm_checkheap(0);
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
        memcpy(dest, oldptr, MIN(old_size, size));
        free(oldptr);
        dbg_printf("========realloc()===returning dest=====\n\n");
        // mm_checkheap(0);
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
    // mm_checkheap(0);
    return ret;
}

/*
 * mm_checkheap - 堆一致性检查(debug 用)。现在可以先留空。
 */
void mm_checkheap(int verbose)
{
    size_t *size = mem_heap_lo();
    char *ptr = mem_heap_lo();

    while (mem_heap_hi() > ptr) {
        ptr = ALIGN_PTR(ptr);
        size = (size_t *)ptr;
        ptr += 8 + *size + 1;
    }
    if ((ptr - 1) != mem_heap_hi()) {
        printf("\n\nERROR: MisMatch: \n");
        printf("MEM HEAP HI: %p\n", mem_heap_hi());
        printf("PTR ENDED AT: %p\n", ptr);
        exit(1);
    }
}