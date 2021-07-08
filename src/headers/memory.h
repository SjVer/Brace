#ifndef clox_memory_h
#define clox_memory_h

#include "common.h"

// duplicates capacity. (sets to 8 if capacity is 0)
#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity)*2)

// frees the memory of the array
#define FREE_ARRAY(type, pointer, oldCount) \
    reallocate(pointer, sizeof(type) * (oldCount), 0)

// grows array to new size using the type of data and the newCount
#define GROW_ARRAY(type, pointer, oldCount, newCount)      \
    (type *)reallocate(pointer, sizeof(type) * (oldCount), sizeof(type) * (newCount))

void *reallocate(void *pointer, size_t oldSize, size_t newSize);

#endif