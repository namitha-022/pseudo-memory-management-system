// memory.c
// Implementation: linked-list allocator with hash index and trie index.
// Note: No globals. All state is inside MemoryManager.

#include "memory.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>

// --- Block metadata (linked list node) ---
typedef struct Block {
    size_t start;       // offset into pool
    size_t size;        // size of block
    int free;           // 1 = free, 0 = allocated
    struct Block *next;
} Block;

// --- Hash table entry (maps start offset to Block*) ---
typedef struct HTEntry {
    size_t key;      // start offset
    Block *blk;
    struct HTEntry *next; // separate chaining
} HTEntry;

// --- Simple bitwise trie node for storing start offsets (demonstrate trie usage) ---
typedef struct TrieNode {
    struct TrieNode *child[2];
    int is_end; // 1 if a stored key ends here
} TrieNode;

// --- Manager struct that holds everything ---
struct MemoryManager {
    unsigned char *pool; // allocated memory pool
    size_t pool_size;

    Block *blocks; // head of linked list of blocks

    // hash index for start->block
    HTEntry **htable;
    size_t htable_size;

    // trie root
    TrieNode *trie_root;
};

// --- utility functions ---
static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) {
        fprintf(stderr, "malloc(%zu) failed\n", n);
        return NULL;
    }
    return p;
}

/* ---------------------- TRIE ---------------------- */
static TrieNode *trie_create() {
    TrieNode *r = xmalloc(sizeof(TrieNode));
    if (!r) return NULL;
    r->child[0] = r->child[1] = NULL;
    r->is_end = 0;
    return r;
}

static void trie_free_rec(TrieNode *node) {
    if (!node) return;
    trie_free_rec(node->child[0]);
    trie_free_rec(node->child[1]);
    free(node);
}

// store 64-bit key (we only use size_t, but treat bits generically)
static int trie_insert(TrieNode *root, size_t key) {
    TrieNode *cur = root;
    for (int i = (int)(sizeof(size_t)*8 - 1); i >= 0; --i) {
        int b = (key >> i) & 1u;
        if (!cur->child[b]) {
            cur->child[b] = trie_create();
            if (!cur->child[b]) return -1;
        }
        cur = cur->child[b];
    }
    cur->is_end = 1;
    return 0;
}

static int trie_remove(TrieNode *root, size_t key) {
    // removal without aggressive pruning (keeps impl simple)
    TrieNode *cur = root;
    for (int i = (int)(sizeof(size_t)*8 - 1); i >= 0; --i) {
        int b = (key >> i) & 1u;
        if (!cur->child[b]) return -1;
        cur = cur->child[b];
    }
    if (!cur->is_end) return -1;
    cur->is_end = 0;
    return 0;
}

/* ---------------------- HASH TABLE ---------------------- */

// allocate hash table array of given size
static HTEntry **ht_create(size_t buckets) {
    HTEntry **arr = xmalloc(sizeof(HTEntry*) * buckets);
    if (!arr) return NULL;
    for (size_t i = 0; i < buckets; ++i) arr[i] = NULL;
    return arr;
}

static void ht_destroy(HTEntry **ht, size_t buckets) {
    if (!ht) return;
    for (size_t i = 0; i < buckets; ++i) {
        HTEntry *e = ht[i];
        while (e) {
            HTEntry *n = e->next;
            free(e);
            e = n;
        }
    }
    free(ht);
}

static size_t ht_hash(size_t key, size_t buckets) {
    // Knuth-ish multiplicative hash for size_t
    return (key * 11400714819323198485uLL) % buckets;
}

static int ht_insert(HTEntry **ht, size_t buckets, size_t key, Block *blk) {
    size_t idx = ht_hash(key, buckets);
    HTEntry *entry = xmalloc(sizeof(HTEntry));
    if (!entry) return -1;
    entry->key = key;
    entry->blk = blk;
    entry->next = ht[idx];
    ht[idx] = entry;
    return 0;
}

static Block *ht_find(HTEntry **ht, size_t buckets, size_t key) {
    size_t idx = ht_hash(key, buckets);
    HTEntry *e = ht[idx];
    while (e) {
        if (e->key == key) return e->blk;
        e = e->next;
    }
    return NULL;
}

static int ht_remove(HTEntry **ht, size_t buckets, size_t key) {
    size_t idx = ht_hash(key, buckets);
    HTEntry *e = ht[idx];
    HTEntry *prev = NULL;
    while (e) {
        if (e->key == key) {
            if (prev) prev->next = e->next;
            else ht[idx] = e->next;
            free(e);
            return 0;
        }
        prev = e;
        e = e->next;
    }
    return -1;
}

/* ---------------------- BLOCK LIST helpers ---------------------- */

static Block *block_create(size_t start, size_t size, int free_flag) {
    Block *b = xmalloc(sizeof(Block));
    if (!b) return NULL;
    b->start = start;
    b->size = size;
    b->free = free_flag;
    b->next = NULL;
    return b;
}

// find first-fit free block with size >= requested
static Block *find_first_fit(Block *head, size_t size) {
    Block *cur = head;
    while (cur) {
        if (cur->free && cur->size >= size) return cur;
        cur = cur->next;
    }
    return NULL;
}

/* ---------------------- PUBLIC API ---------------------- */

MemoryManager *mm_create(size_t pool_size) {
    if (pool_size == 0) { errno = EINVAL; return NULL; }
    MemoryManager *mm = xmalloc(sizeof(MemoryManager));
    if (!mm) return NULL;

    mm->pool = xmalloc(pool_size);
    if (!mm->pool) { free(mm); return NULL; }
    mm->pool_size = pool_size;

    // create one free block covering entire pool
    mm->blocks = block_create(0, pool_size, 1);
    if (!mm->blocks) { free(mm->pool); free(mm); return NULL; }

    // choose small hash table size proportional to blocks; pick a prime-ish count
    mm->htable_size = 4099; // arbitrary moderate size
    mm->htable = ht_create(mm->htable_size);
    if (!mm->htable) { free(mm->blocks); free(mm->pool); free(mm); return NULL; }

    // insert initial block into hash and trie
    if (ht_insert(mm->htable, mm->htable_size, mm->blocks->start, mm->blocks) != 0) {
        ht_destroy(mm->htable, mm->htable_size);
        free(mm->blocks); free(mm->pool); free(mm);
        return NULL;
    }

    mm->trie_root = trie_create();
    if (!mm->trie_root) {
        ht_destroy(mm->htable, mm->htable_size);
        free(mm->blocks); free(mm->pool); free(mm);
        return NULL;
    }
    if (trie_insert(mm->trie_root, mm->blocks->start) != 0) {
        trie_free_rec(mm->trie_root);
        ht_destroy(mm->htable, mm->htable_size);
        free(mm->blocks); free(mm->pool); free(mm);
        return NULL;
    }

    return mm;
}

void mm_destroy(MemoryManager *mm) {
    if (!mm) return;
    // free block list
    Block *b = mm->blocks;
    while (b) {
        Block *n = b->next;
        free(b);
        b = n;
    }
    trie_free_rec(mm->trie_root);
    ht_destroy(mm->htable, mm->htable_size);
    free(mm->pool);
    free(mm);
}

void *mm_alloc(MemoryManager *mm, size_t size) {
    if (!mm || size == 0) { errno = EINVAL; return NULL; }
    // minimal alignment: 1 byte (could change)
    Block *fit = find_first_fit(mm->blocks, size);
    if (!fit) return NULL;

    if (fit->size > size) {
        // split block: create remainder block
        Block *rem = block_create(fit->start + size, fit->size - size, 1);
        if (!rem) return NULL;
        rem->next = fit->next;
        fit->next = rem;
        // insert rem to hash and trie
        if (ht_insert(mm->htable, mm->htable_size, rem->start, rem) != 0) {
            // rollback
            fit->next = rem->next;
            free(rem);
            return NULL;
        }
        if (trie_insert(mm->trie_root, rem->start) != 0) {
            ht_remove(mm->htable, mm->htable_size, rem->start);
            fit->next = rem->next;
            free(rem);
            return NULL;
        }
        fit->size = size;
    }

    // mark allocated
    fit->free = 0;
    // update hash/trie mapping: existing key maps to same block (already present),
    // but it might have been free earlier. If it wasn't present (should be) insert:
    if (ht_find(mm->htable, mm->htable_size, fit->start) == NULL) {
        ht_insert(mm->htable, mm->htable_size, fit->start, fit);
        trie_insert(mm->trie_root, fit->start);
    }

    return (void*)(mm->pool + fit->start);
}

int mm_free(MemoryManager *mm, void *ptr) {
    if (!mm || !ptr) { errno = EINVAL; return -1; }
    // verify pointer is inside pool and aligned
    uintptr_t pool_addr = (uintptr_t)mm->pool;
    uintptr_t p = (uintptr_t)ptr;
    if (p < pool_addr || p >= pool_addr + mm->pool_size) { errno = EINVAL; return -1; }
    size_t offset = (size_t)(p - pool_addr);

    Block *b = ht_find(mm->htable, mm->htable_size, offset);
    if (!b) {
        // not found in hash -> search list (fallback)
        Block *cur = mm->blocks;
        while (cur) {
            if (cur->start == offset) { b = cur; break; }
            cur = cur->next;
        }
        if (!b) { errno = EINVAL; return -1; }
    }

    if (b->free) { // double free
        errno = EFAULT;
        return -1;
    }

    b->free = 1;
    // keep indexes: hash and trie remain mapping start->block (fine).
    return 0;
}

void *mm_realloc(MemoryManager *mm, void *ptr, size_t new_size) {
    if (!mm) { errno = EINVAL; return NULL; }
    if (!ptr) return mm_alloc(mm, new_size);
    if (new_size == 0) { mm_free(mm, ptr); return NULL; }

    uintptr_t pool_addr = (uintptr_t)mm->pool;
    uintptr_t p = (uintptr_t)ptr;
    if (p < pool_addr || p >= pool_addr + mm->pool_size) { errno = EINVAL; return NULL; }
    size_t offset = (size_t)(p - pool_addr);

    Block *b = ht_find(mm->htable, mm->htable_size, offset);
    if (!b) {
        // fallback search
        Block *cur = mm->blocks;
        while (cur) { if (cur->start == offset) { b = cur; break; } cur = cur->next; }
        if (!b) { errno = EINVAL; return NULL; }
    }

    if (new_size <= b->size) {
        // shrink in place
        size_t extra = b->size - new_size;
        if (extra > 0) {
            // create a new free block after current
            Block *rem = block_create(b->start + new_size, extra, 1);
            if (!rem) return NULL;
            rem->next = b->next;
            b->next = rem;
            if (ht_insert(mm->htable, mm->htable_size, rem->start, rem) != 0) {
                b->next = rem->next; free(rem); return NULL;
            }
            trie_insert(mm->trie_root, rem->start);
            b->size = new_size;
        }
        return ptr;
    } else {
        // try to see if next block is free and enough to expand in place
        if (b->next && b->next->free && (b->size + b->next->size) >= new_size) {
            size_t need = new_size - b->size;
            if (b->next->size > need) {
                // reduce next block
                b->next->start += need;
                b->next->size -= need;
                b->size = new_size;
                // update hash/trie for changed next->start: remove old entry, insert new
                // For simplicity, remove old and insert new
                // (we must find the old key: old_start = b->next->start - need)
                size_t old_start = b->next->start - need;
                ht_remove(mm->htable, mm->htable_size, old_start);
                trie_remove(mm->trie_root, old_start);
                ht_insert(mm->htable, mm->htable_size, b->next->start, b->next);
                trie_insert(mm->trie_root, b->next->start);
                return ptr;
            } else {
                // consume entire next block and potentially more (if adjacent)
                b->size += b->next->size;
                Block *t = b->next;
                b->next = t->next;
                ht_remove(mm->htable, mm->htable_size, t->start);
                trie_remove(mm->trie_root, t->start);
                free(t);
                if (b->size >= new_size) {
                    // maybe split remainder
                    if (b->size > new_size) {
                        Block *rem = block_create(b->start + new_size, b->size - new_size, 1);
                        if (!rem) return NULL;
                        rem->next = b->next;
                        b->next = rem;
                        ht_insert(mm->htable, mm->htable_size, rem->start, rem);
                        trie_insert(mm->trie_root, rem->start);
                        b->size = new_size;
                    }
                    return ptr;
                }
            }
        }

        // fallback: allocate new block, copy, free old
        void *newptr = mm_alloc(mm, new_size);
        if (!newptr) return NULL;
        // copy old data (b->size bytes)
        memcpy(newptr, ptr, b->size);
        mm_free(mm, ptr);
        return newptr;
    }
}

int mm_gc(MemoryManager *mm) {
    if (!mm) { errno = EINVAL; return -1; }
    int merges = 0;
    Block *cur = mm->blocks;
    while (cur && cur->next) {
        if (cur->free && cur->next->free) {
            // merge
            Block *rem = cur->next;
            cur->size += rem->size;
            cur->next = rem->next;
            // remove rem start from indexes
            ht_remove(mm->htable, mm->htable_size, rem->start);
            trie_remove(mm->trie_root, rem->start);
            free(rem);
            merges++;
            continue; // check again with same cur (maybe more merges)
        }
        cur = cur->next;
    }
    return merges;
}

void mm_stats(MemoryManager *mm) {
    if (!mm) return;
    size_t free_mem = 0, used_mem = 0;
    Block *cur = mm->blocks;
    printf("POOL SIZE: %zu bytes\n", mm->pool_size);
    printf("Blocks:\n");
    while (cur) {
        printf("  start=%6zu size=%6zu %s\n", cur->start, cur->size, cur->free ? "FREE" : "USED");
        if (cur->free) free_mem += cur->size; else used_mem += cur->size;
        cur = cur->next;
    }
    printf("TOTAL USED: %zu, TOTAL FREE: %zu\n", used_mem, free_mem);
}

void mm_debug_print_blocks(MemoryManager *mm) {
    if (!mm) return;
    printf("### BLOCK LIST TRACE ###\n");
    Block *cur = mm->blocks;
    while (cur) {
        printf("-> [start=%zu size=%zu %s]\n", cur->start, cur->size, cur->free ? "FREE":"USED");
        cur = cur->next;
    }
}
