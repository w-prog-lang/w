#ifndef WLANG_UTIL_H
#define WLANG_UTIL_H

#include <stddef.h>

typedef struct ArenaBlock {
    struct ArenaBlock* next;
    size_t size;
    size_t used;
    char data[];
} ArenaBlock;

typedef struct {
    ArenaBlock* head;
} Arena;

void arena_init(Arena* a);
void* arena_alloc(Arena* a, size_t size);
void arena_free(Arena* a);

typedef struct {
    void** items;
    int count;
    int cap;
} PtrList;

void ptrlist_init(PtrList* l);
void ptrlist_push(PtrList* l, void* item);

#endif
