#ifndef clox_compiler_h
#define clox_compiler_h

#include "vm.h"

// main compile function
bool compile(const char *source, Chunk *chunk);

#endif