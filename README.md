# Pseudo Memory Management System

A user-space memory manager written in C. It reserves one large block of memory (a *pool*) and then hands out and reclaims pieces of that pool itself, the way `malloc`/`free` do. An interactive menu-driven program lets you allocate, free, reallocate and inspect memory, so you can watch splitting, fragmentation and coalescing happen.

This project is meant for learning how dynamic memory allocators work.

## Features

- **Fixed-size pool**: one `malloc`'d pool (256 to 1,000,000 bytes) managed entirely by the library.
- **First-fit allocation**: scans the block list for the first free block large enough, splitting off any remainder.
- **Free with double-free detection**: freeing an already-free block returns an error.
- **Reallocation**: shrinks in place, grows in place when the next block is free and large enough, otherwise allocates, copies and frees.
- **Garbage collection / coalescing**: `mm_gc` merges adjacent free blocks to reduce fragmentation and reports how many merges it did.
- **Indexes**: a hash table (separate chaining, 4099 buckets) maps a block's start offset to its metadata for fast lookup on `free`/`realloc`. A bitwise trie also stores block start offsets to demonstrate trie usage.
- **No global state**: everything lives inside the `MemoryManager` struct, so multiple managers can coexist.
- **Diagnostics**: `mm_stats` and `mm_debug_print_blocks` print the current block layout.

## Project structure

| File | Purpose |
|------|---------|
| `memory.h` | Public API of the memory manager |
| `memory.c` | Implementation: block linked list, hash table, trie |
| `main.c` | Interactive command-line demo |
| `Makefile` | Build rules |

## Getting started

### Requirements

- `gcc` (C11 support)
- `make`

### Build

```bash
make
```

This compiles with `-std=c11 -O2 -Wall -Wextra -g` and produces the `pm_manager` executable.

### Run

```bash
./pm_manager
```

### Clean

```bash
make clean
```

## Usage

On start you enter the pool size in bytes (256 to 1,000,000). Then a menu appears:

```
1. Allocate Memory
2. Free Memory
3. Garbage Collect
4. Show Statistics
5. Reallocate Memory
6. Show Allocated Blocks
7. Exit
```

| Option | What it does |
|--------|--------------|
| 1 | Asks for a size, allocates from the pool, prints the address and the index it is tracked under |
| 2 | Frees the allocation at a given index |
| 3 | Runs the garbage collector and reports how many adjacent free blocks were merged |
| 4 | Prints the pool size, every block (start, size, FREE/USED) and total used/free bytes |
| 5 | Resizes the allocation at a given index |
| 6 | Lists all currently active allocations |
| 7 | Destroys the manager and exits |

The demo tracks up to 100 allocations (`MAX_PTRS`).

### Example session

```
Enter memory pool size (256 to 1,000,000 bytes): 1024
✓ Memory manager initialized with 1024 bytes

Enter your choice: 1
Enter size to allocate: 100
✓ Allocated 100 bytes at address: 0x...
  Stored at index: 0

Enter your choice: 4
POOL SIZE: 1024 bytes
Blocks:
  start=     0 size=   100 USED
  start=   100 size=   924 FREE
TOTAL USED: 100, TOTAL FREE: 924
```

## API reference

Declared in `memory.h`:

```c
MemoryManager *mm_create(size_t pool_size);
void           mm_destroy(MemoryManager *mm);
void          *mm_alloc(MemoryManager *mm, size_t size);
int            mm_free(MemoryManager *mm, void *ptr);
void          *mm_realloc(MemoryManager *mm, void *ptr, size_t new_size);
int            mm_gc(MemoryManager *mm);
void           mm_stats(MemoryManager *mm);
void           mm_debug_print_blocks(MemoryManager *mm);
```

| Function | Description | Returns |
|----------|-------------|---------|
| `mm_create` | Creates a manager with a pool of `pool_size` bytes | Manager, or `NULL` on error |
| `mm_destroy` | Frees the pool and all metadata; the pointer is invalid afterwards | none |
| `mm_alloc` | Allocates `size` bytes from the pool (first fit) | Pointer inside the pool, or `NULL` |
| `mm_free` | Releases a block previously returned by `mm_alloc` | `0` on success, `-1` on error (invalid pointer or double free) |
| `mm_realloc` | Resizes a block; the old block stays valid if resizing fails | New pointer, or `NULL` |
| `mm_gc` | Coalesces adjacent free blocks | Number of merges, or `-1` on error |
| `mm_stats` | Prints pool size, block list and used/free totals | none |
| `mm_debug_print_blocks` | Prints the raw block list for debugging | none |

### Using it in your own code

```c
#include "memory.h"

int main(void) {
    MemoryManager *mm = mm_create(4096);
    if (!mm) return 1;

    char *a = mm_alloc(mm, 128);
    char *b = mm_alloc(mm, 256);

    mm_free(mm, a);
    mm_free(mm, b);
    mm_gc(mm);          // merge the two freed blocks back together
    mm_stats(mm);

    mm_destroy(mm);
    return 0;
}
```

Compile with `gcc -std=c11 your_file.c memory.c -o your_program`.

## How it works

1. **Pool**: `mm_create` allocates one contiguous pool and a single free `Block` covering all of it.
2. **Blocks**: each `Block` records its start offset, size, free flag and a pointer to the next block, forming a linked list in address order.
3. **Allocate**: find the first free block that fits. If it is larger than requested, split it into a used block and a free remainder.
4. **Free**: look the block up through the hash table by its offset (falling back to a list scan) and mark it free. Neighbours are not merged immediately.
5. **Garbage collect**: walk the list and merge every run of adjacent free blocks into one.
6. **Realloc**: shrink by splitting off the tail, grow by absorbing a following free block, or fall back to allocate, `memcpy`, free.

## Limitations

- Allocations are not aligned beyond 1 byte.
- Freed blocks are only merged when `mm_gc` is called (or via realloc), not automatically on free.
- First-fit can fragment the pool over time.
- The trie is maintained alongside the hash table for demonstration but is not used for lookups.
- The pool size is fixed at creation and cannot grow.
- Not thread-safe.

## License

No license has been specified for this repository yet.
