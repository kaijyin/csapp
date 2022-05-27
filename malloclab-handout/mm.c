/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1<<12)

#define MAX(x,y) ((x)>(y)?(x):(y))

/*get header or footer packed*/
#define PACK(size,alloc) ((size) | alloc)

/*get val or put val at point p*/
#define GET(p)  (*(unsigned int *)(p))
#define GETP(p)  (*(char **)(p))
#define PUT(p,val)  (*(unsigned int *)(p) = (val))
#define PUTP(p,val)  (*(char **)(p) = (val))

#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)


#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NXTP(bp) ((char *)(bp) + WSIZE)
#define PRVP(bp) ((char *)(bp))
  
#define NEXT_BLKP(bp) ((char*)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char*)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

void *free_listp;
static void *extend_heap(size_t words);

static void *coalesce(void *bp);
static void *find_fit(size_t size);
static void place(void *bp,size_t size,size_t alloc);
static void insert(void*bp);
static void move(void*bp);
/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void){
    void *bp;  
    if((bp=mem_sbrk(4*WSIZE)) == (void *)-1){
        return -1;
    }
    PUT(bp,0);
    //添加序言块和结尾快,避免多余的边界检查(边界不会coalesce),相当于哨兵
    PUT(bp + (1*WSIZE),PACK(DSIZE, 1));
    PUT(bp + (2*WSIZE),PACK(DSIZE, 1));
    PUT(bp + (3*WSIZE),PACK(0, 1));
    free_listp=NULL;
    //先预分配空闲块
    if(extend_heap(CHUNKSIZE/WSIZE)==NULL)
      return -1;
    return 0;
}
/*扩展堆大小*/
static void * extend_heap(size_t words){
    char *bp;
    size_t size;

    /*按照DSIZE对齐*/
    size=(words%2)?(words+1)*WSIZE:words*WSIZE;
    if((long)(bp=mem_sbrk(size))==-1)
      return NULL;
    //设置新块flag,原本的结尾块作为新块的Head,这里不要用place,不需要删除块
    PUT(HDRP(bp),PACK(size,0));
    PUT(FTRP(bp),PACK(size,0));
    //添加新的结尾块
    PUT(HDRP(NEXT_BLKP(bp)),PACK(0,1));
    //先合并,再插入
    bp=coalesce(bp);
    insert(bp);
    return bp;
}
static void insert(void *bp){
    if(free_listp==NULL){//空链表特判
        PUTP(PRVP(bp),NULL);
        PUTP(NXTP(bp),NULL);
        free_listp=bp;
        return ;
    }
    PUTP(PRVP(bp),NULL);
    PUTP(PRVP(free_listp),bp);
    PUTP(NXTP(bp),free_listp);
    free_listp=bp;
}
static void move(void *bp){
    if(free_listp==bp){//头部特判
        free_listp=GETP(NXTP(bp));
        if(free_listp!=NULL){
           PUTP(PRVP(free_listp),NULL);
        }
        return;
    }
    PUTP(NXTP(PRVP(bp)),NXTP(bp));
    if(NXTP(bp)!=NULL){//尾部特判
        PUTP(PRVP((NXTP(bp))),PRVP(bp));
    }
}
/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    char *bp;
    if(size==0){
        return NULL;
    }
    //size按照DSIZE内存对齐
    if(size<=DSIZE){
        asize=2*DSIZE;
    }else{
        asize=DSIZE*((size+(DSIZE)+(DSIZE-1))/DSIZE);
    }
    //找合适的空闲块
    if((bp=find_fit(asize))!=NULL){
        size_t cursize=GET_SIZE(HDRP(bp));
        move(bp);
        if(cursize>asize){
          place(((char*)bp)+asize,cursize-asize,0);
          insert(((char*)bp)+asize);
        }
        place(bp,asize,1);
        return bp;
    }
    //找不到就申请新块
    extendsize = MAX(asize,CHUNKSIZE);
    if(( bp = extend_heap(extendsize/WSIZE)) == NULL)
      return NULL;
    move(bp);
    place(bp,asize,1);
    return bp;
}

static void print_freelist(){
    void *bp=free_listp;
    printf("------------------free_list--------------------\n");
    size_t cursize;
    int mx=5;
    while(bp!=NULL&&mx){
        cursize=GET_SIZE(HDRP(bp));
        printf("ptr:%p size:%d\n",bp,cursize);
        bp=GETP(NXTP(bp));
        mx--;
    }
    printf("\n");
}
static void* find_fit(size_t size){
    if(free_listp==NULL){
        return NULL;
    }
    void *bp=free_listp;
    size_t cursize;
    while(bp!=NULL){
        cursize=GET_SIZE(HDRP(bp));
        if(cursize>=size){
            break;
        }
        bp=GETP(NXTP(bp));
    }
    return bp;
}
//set the free block been placed
static void place(void *bp,size_t size,size_t alloc){
      PUT(HDRP(bp),PACK(size,alloc));
      PUT(FTRP(bp),PACK(size,alloc));
    //   print_freelist();
}
/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    size_t size=GET_SIZE(HDRP(bp));
    PUT(HDRP(bp),PACK(size,0));
    PUT(FTRP(bp),PACK(size,0));
    bp=coalesce(bp);
    insert(bp);
}
static void*coalesce(void *bp){
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    void *prev_blkp=PREV_BLKP(bp);
    void *next_blkp=NEXT_BLKP(bp);
    size_t size = GET_SIZE(HDRP(bp));
    if(prev_alloc&&next_alloc){
        return bp;
    }
    else if(prev_alloc && !next_alloc){
        //从空闲链表中移出next_block
        move(next_blkp);

        size+=GET_SIZE(HDRP(next_blkp));
        PUT(HDRP(bp),PACK(size,0));
        PUT(FTRP(next_blkp),PACK(size,0));
    }
    else if(!prev_alloc && next_alloc){
        //从空闲链表中移出pre_block
        move(prev_blkp);
        
        size+=GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(HDRP(prev_blkp),PACK(size,0));
        PUT(FTRP(bp),PACK(size,0));
        bp = prev_blkp;
    }
    else {
        move(prev_blkp);
        move(next_blkp);

        size+=GET_SIZE(HDRP(PREV_BLKP(bp)))+GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(prev_blkp),PACK(size,0));
        PUT(FTRP(next_blkp),PACK(size,0));
        bp=prev_blkp;
    }
    return bp;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    
    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;
    copySize = GET_SIZE((HDRP(oldptr)))-DSIZE;//只复制数据
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}














