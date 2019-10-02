////////////////////////////////////////////////////////////////////////////////
//
//  File           : block_cache.c
//  Description    : This is the implementation of the cache for the BLOCK
//                   driver.
//
//  Author         : Chloe Gregory
//  Last Modified  : 8/7/19
//

// Includes
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

// Project includes
#include <block_cache.h>
#include <cmpsc311_log.h>

extern void* memcpy(void* destination, const void* source, size_t num);

uint32_t block_cache_max_items = DEFAULT_BLOCK_FRAME_CACHE_SIZE; // Maximum number of items in cache

typedef struct CacheNode {
	BlockIndex nBlock;
	BlockFrameIndex nFrm;
	struct CacheNode *next;
	char nbuf[4096];
} CacheNode;

typedef struct Cache {
    uint32_t currentSize;
    CacheNode *head;
} Cache;

Cache *cache;

CacheNode* createNewNode(BlockIndex nBlock,BlockFrameIndex nFrm, CacheNode *next, char *nBuf);

//
// Functions

////////////////////////////////////////////////////////////////////////////////
//
// Function     : createNewNode
// Description  : Allocates space for new nodes as they're added
//
CacheNode* createNewNode(BlockIndex nBlock,BlockFrameIndex nFrm, CacheNode *next, char *nBuf){
	CacheNode *newNode = calloc(1,sizeof(CacheNode));
	memcpy(newNode->nbuf,nBuf,4096);
	newNode->nBlock = nBlock;
	newNode->nFrm = nFrm;
	newNode->next = next;
	return newNode;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : set_block_cache_size
// Description  : Set the size of the cache (must be called before init)
//
// Inputs       : max_frames - the maximum number of items your cache can hold
// Outputs      : 0 if successful, -1 if failure

int set_block_cache_size(uint32_t max_frames)
{
    block_cache_max_items=max_frames;
    return (0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : init_block_cache
// Description  : Initialize the cache and note maximum frames
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int init_block_cache(void)
{
	cache = calloc(1,sizeof(Cache)); 
	if (cache == NULL)
		return -1;
	cache->currentSize = 0;
	cache->head = NULL;
    return (0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : close_block_cache
// Description  : Clear all of the contents of the cache, cleanup
//
// Inputs       : none
// Outputs      : o if successful, -1 if failure

int close_block_cache(void)
{
	while(cache->head != NULL) {
		CacheNode *oldHead = cache->head;
		cache->head = cache->head->next;
		free(oldHead);
		oldHead = NULL;
	}
	free(cache);
	cache = NULL;
    return (0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : put_block_cache
// Description  : Put an object into the frame cache
//
// Inputs       : block - the block number of the frame to cache
//                frm - the frame number of the frame to cache
//                buf - the buffer to insert into the cache
// Outputs      : 0 if successful, -1 if failure

int put_block_cache(BlockIndex block, BlockFrameIndex frm, void* buf)
{
	if (cache->currentSize == 0) {
		CacheNode *newNode = createNewNode(block,frm,NULL,buf);
		cache->head = newNode;
		cache->currentSize++;
		return (0);
	}
	else if (cache->currentSize == 1) {
		if(cache->head->nFrm==frm) {
			if (cache->head->nbuf!=buf)
				memcpy(cache->head->nbuf,buf,4096);
			return 0;
		}
		CacheNode *newNode = createNewNode(block,frm,cache->head,buf);
		cache->head = newNode;
		cache->currentSize++;
		return (0);
	}
	else if (cache->currentSize == block_cache_max_items) {
		if(cache->head->nFrm==frm) {
				if (cache->head->nbuf!=buf)
					memcpy(cache->head->nbuf,buf,4096);
				return 0;
		}
		CacheNode *iter, *previter;
		for (iter = cache->head; (iter->next!=NULL)&&(iter->next->nFrm != frm); iter=iter->next){
			previter=iter;
		}
		if (iter->next != NULL) {
			CacheNode *popVal = iter->next;
			if (popVal->nbuf!=buf)
				memcpy(popVal->nbuf,buf,4096);
			iter->next = popVal->next;
			popVal->next = cache->head;
			cache->head = popVal;
			return (0);
		}
		else{
			CacheNode *popVal = previter->next;
			popVal->nBlock = block;
			popVal->nFrm = frm;
			if (popVal->nbuf!=buf)
				memcpy(popVal->nbuf,buf,4096);
			previter->next = popVal->next;
			popVal->next = cache->head;
			cache->head = popVal;
			return (0);
		}	
	}
	else {
		if(cache->head->nFrm==frm) {
			if (cache->head->nbuf!=buf)
				memcpy(cache->head->nbuf,buf,4096);
			return 0;
		}
		CacheNode *iter, *previter;
		for (iter = cache->head; (iter->next!=NULL)&&(iter->nFrm != frm); iter=iter->next){
			previter=iter;
		}
		if (iter->next != NULL) {
			CacheNode *popVal = previter->next;
			previter->next = popVal->next;
			if (popVal->nbuf!=buf)
				memcpy(popVal->nbuf,buf,4096);
			popVal->next = cache->head;
			cache->head = popVal;
			return (0);
		}
		else{
			if (iter->nFrm==frm) {
				CacheNode *popVal = previter->next;
				if (popVal->nbuf!=buf)
					memcpy(popVal->nbuf,buf,4096);
				previter->next = popVal->next;
				popVal->next = cache->head;
				cache->head = popVal;
				return (0);
			}
			else {
				CacheNode *newNode = createNewNode(block,frm,cache->head,buf);
				cache->head = newNode;
				cache->currentSize++;
				return (0);
			}
		}	
	}
    return (0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : get_block_cache
// Description  : Get an frame from the cache (and return it)
//
// Inputs       : block - the block number of the block to find
//                frm - the  number of the frame to find
// Outputs      : pointer to cached frame or NULL if not found

void* get_block_cache(BlockIndex block, BlockFrameIndex frm)
{
	if(cache->head==NULL){
		return NULL;
	}
	CacheNode *iter = cache->head;
	if(iter->nFrm==frm){
		return &(iter->nbuf);
	}
	while ((iter->next != NULL)&&(iter->nFrm!=frm)) {
		iter = iter->next;
	}
	if (iter->nFrm==frm) {
		return &(iter->nbuf);
	}
    return (NULL);
}


//
// Unit test

////////////////////////////////////////////////////////////////////////////////
//
// Function     : blockCacheUnitTest
// Description  : Run a UNIT test checking the cache implementation
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int blockCacheUnitTest(void)
{
    // Return successfully
    logMessage(LOG_OUTPUT_LEVEL, "Cache unit test completed successfully.");
    return (0);
}
