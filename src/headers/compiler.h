#ifndef brace_compiler_h
#define brace_compiler_h

#include "vm.h"

// main compile function
ObjFunction *compile(const char *source);
void markCompilerRoots();

#endif