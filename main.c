#include <stdio.h>
#include <stdlib.h>
#include "memory.h"

#define MAX_PTRS 100

void clear_input_buffer() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

int main() {
    size_t pool_size;
    printf("═══════════════════════════════════════\n");
    printf("    PSEUDO MEMORY MANAGER\n");
    printf("═══════════════════════════════════════\n");
    printf("Enter memory pool size (256 to 1,000,000 bytes): ");
    fflush(stdout);

    while (1) {
        if (scanf("%zu", &pool_size) != 1) {
            printf("Invalid input. Enter a number: ");
            clear_input_buffer();
            continue;
        }

        if (pool_size < 256) {
            printf("Too small. Enter >= 256: ");
            clear_input_buffer();
            continue;
        }
        if (pool_size > 1000000) {
            printf("Too large. Enter <= 1,000,000: ");
            clear_input_buffer();
            continue;
        }
        break;
    }
    clear_input_buffer();

    MemoryManager *mm = mm_create(pool_size);
    if (!mm) {
        printf("ERROR: Failed to create memory manager!\n");
        return 1;
    }

    printf("✓ Memory manager initialized with %zu bytes\n\n", pool_size);

    void *user_ptrs[MAX_PTRS] = {0};
    int ptr_count = 0;

    int choice;
    while (1) {
        printf("\n═══════════════════════════════════════\n");
        printf("              MENU\n");
        printf("═══════════════════════════════════════\n");
        printf("1. Allocate Memory\n");
        printf("2. Free Memory\n");
        printf("3. Garbage Collect\n");
        printf("4. Show Statistics\n");
        printf("5. Reallocate Memory\n");
        printf("6. Show Allocated Blocks\n");
        printf("7. Exit\n");
        printf("═══════════════════════════════════════\n");
        printf("Enter your choice: ");
        fflush(stdout);
        
        if (scanf("%d", &choice) != 1) {
            printf("Invalid input!\n");
            clear_input_buffer();
            continue;
        }
        clear_input_buffer();

        if (choice == 1) {
            // Allocate Memory
            size_t size;
            printf("Enter size to allocate: ");
            fflush(stdout);
            
            if (scanf("%zu", &size) != 1) {
                printf("Invalid size!\n");
                clear_input_buffer();
                continue;
            }
            clear_input_buffer();

            if (size == 0) {
                printf("Size must be > 0\n");
                continue;
            }

            void *p = mm_alloc(mm, size);
            if (p) {
                printf("✓ Allocated %zu bytes at address: %p\n", size, p);

                if (ptr_count < MAX_PTRS) {
                    user_ptrs[ptr_count] = p;
                    printf("  Stored at index: %d\n", ptr_count);
                    ptr_count++;
                } else {
                    printf("⚠ Warning: Max pointers reached. Cannot track this allocation.\n");
                }
            } else {
                printf("✗ Allocation failed. Not enough memory.\n");
                printf("  Tip: Try running Garbage Collection (option 3)\n");
            }
        }

        else if (choice == 2) {
            // Free Memory
            if (ptr_count == 0) {
                printf("No allocations to free!\n");
                continue;
            }

            printf("Enter index of pointer to free (0-%d): ", ptr_count - 1);
            fflush(stdout);
            int idx;
            
            if (scanf("%d", &idx) != 1) {
                printf("Invalid index!\n");
                clear_input_buffer();
                continue;
            }
            clear_input_buffer();

            if (idx < 0 || idx >= ptr_count) {
                printf("Index out of range (0-%d)\n", ptr_count - 1);
                continue;
            }

            if (user_ptrs[idx] == NULL) {
                printf("This pointer was already freed!\n");
                continue;
            }

            if (mm_free(mm, user_ptrs[idx]) == 0) {
                printf("✓ Freed pointer at index %d\n", idx);
                user_ptrs[idx] = NULL;
            } else {
                printf("✗ Free failed!\n");
            }
        }

        else if (choice == 3) {
            // Garbage Collect
            printf("Running garbage collector...\n");
            int merges = mm_gc(mm);
            if (merges > 0) {
                printf("✓ GC completed. Merged %d adjacent free blocks.\n", merges);
            } else {
                printf("✓ GC completed. No blocks to merge.\n");
            }
        }

        else if (choice == 4) {
            // Show Statistics
            mm_stats(mm);
        }

        else if (choice == 5) {
            // Reallocate Memory
            if (ptr_count == 0) {
                printf("No allocations to reallocate!\n");
                continue;
            }

            printf("Enter index of pointer to reallocate (0-%d): ", ptr_count - 1);
            fflush(stdout);
            int idx;
            
            if (scanf("%d", &idx) != 1) {
                printf("Invalid index!\n");
                clear_input_buffer();
                continue;
            }
            clear_input_buffer();

            if (idx < 0 || idx >= ptr_count) {
                printf("Index out of range (0-%d)\n", ptr_count - 1);
                continue;
            }

            if (user_ptrs[idx] == NULL) {
                printf("This pointer was already freed!\n");
                continue;
            }

            size_t new_size;
            printf("Enter new size: ");
            fflush(stdout);
            
            if (scanf("%zu", &new_size) != 1) {
                printf("Invalid size!\n");
                clear_input_buffer();
                continue;
            }
            clear_input_buffer();

            void *new_ptr = mm_realloc(mm, user_ptrs[idx], new_size);
            if (new_ptr) {
                printf("✓ Reallocated. New address: %p\n", new_ptr);
                user_ptrs[idx] = new_ptr;
            } else {
                printf("✗ Reallocation failed.\n");
            }
        }

        else if (choice == 6) {
            // Show Allocated Blocks
            printf("\n════════════════════════════════════════\n");
            printf("       ACTIVE ALLOCATIONS\n");
            printf("════════════════════════════════════════\n");
            int found = 0;
            for (int i = 0; i < ptr_count; i++) {
                if (user_ptrs[i] != NULL) {
                    printf("[%d] Address: %p\n", i, user_ptrs[i]);
                    found = 1;
                }
            }
            if (!found) {
                printf("  (No active allocations)\n");
            }
            printf("════════════════════════════════════════\n");
        }

        else if (choice == 7) {
            // Exit
            printf("\n✓ Cleaning up and exiting...\n");
            break;
        }

        else {
            printf("✗ Invalid choice. Please enter 1-7.\n");
        }
    }

    mm_destroy(mm);
    printf("✓ Memory manager destroyed successfully.\n");
    return 0;
}