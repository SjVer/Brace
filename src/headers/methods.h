#ifndef brace_methods_h
#define brace_methods_h

#include "vm.h"
#include "table.h"

/*
Each value/object type that has methods
has its ValueArray below. These arrays
contain ObjNatives that point to the
methods in "methods.c".
When such a method is bound the native
is put in an ObjBoundNativeMethod that
also holds its receiver. This receiver
gets pushed upon call and will be the
first value in the args array passed
to each native. This is also why the
argCount and arity variables are 
incremented once.
*/

ValueArray numberMethods;
ValueArray stringMethods;
ValueArray arrayMethods;

void defineAllMethods();

#endif // !brace_methods_h