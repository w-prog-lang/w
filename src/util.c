#include "util.h"

#include <stdlib.h>
#include <string.h>

#define ARENA_BLOCK_SIZE (64 * 1024)

void arena_init(Arena* a) { a->head = NULL; }

static ArenaBlock* arena_new_block(size_t min_size) {
    size_t size = min_size > ARENA_BLOCK_SIZE ? min_size : ARENA_BLOCK_SIZE;
    ArenaBlock* b = malloc(sizeof(ArenaBlock) + size);
    b->next = NULL;
    b->size = size;
    b->used = 0;
    return b;
}

void* arena_alloc(Arena* a, size_t size) {
    size = (size + 7) & ~((size_t)7);

    if (!a->head || a->head->used + size > a->head->size) {
        ArenaBlock* b = arena_new_block(size);
        b->next = a->head;
        a->head = b;
    }

    void* ptr = a->head->data + a->head->used;
    a->head->used += size;
    return ptr;
}

void arena_free(Arena* a) {
    ArenaBlock* b = a->head;
    while (b) {
        ArenaBlock* next = b->next;
        free(b);
        b = next;
    }
    a->head = NULL;
}

void ptrlist_init(PtrList* l) {
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

void ptrlist_push(PtrList* l, void* item) {
    if (l->count == l->cap) {
        l->cap = l->cap == 0 ? 8 : l->cap * 2;
        l->items = realloc(l->items, l->cap * sizeof(void*));
    }
    l->items[l->count++] = item;
}
