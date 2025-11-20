// memory.h
// Public API for the pseudo memory manager.

#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>

typedef struct MemoryManager MemoryManager;

// initialize manager: pool_size bytes. returns NULL on error.
MemoryManager *mm_create(size_t pool_size);

// destroy manager and free all metadata. After this pointer invalid.
void mm_destroy(MemoryManager *mm);

// allocate `size` bytes from the pool. returns pointer inside pool or NULL.
void *mm_alloc(MemoryManager *mm, size_t size);

// free a previously allocated pointer. returns 0 on success, -1 on error.
int mm_free(MemoryManager *mm, void *ptr);

// reallocate a block to new_size. returns new pointer or NULL (old stays valid if reallocation fails).
void *mm_realloc(MemoryManager *mm, void *ptr, size_t new_size);

// run garbage collector (coalesce adjacent free blocks). returns number of merges performed.
int mm_gc(MemoryManager *mm);

// print stats to stdout.
void mm_stats(MemoryManager *mm);

// debug: print internal blocks list (for testing).
void mm_debug_print_blocks(MemoryManager *mm);

#endif // MEMORY_H
