#ifndef brace_vm_h
#define brace_vm_h

#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct
{
	ObjClosure *closure;
	uint8_t *ip;
	Value *slots;
} CallFrame;

typedef struct
{
	CallFrame frames[FRAMES_MAX];
	int frameCount;

	Value stack[STACK_MAX];
	Value *stackTop;
	Table nativeVars;
	Table globals;
	Table globalsTypes;
	Table strings;
	ObjString *initString;
	ObjUpvalue *openUpvalues;

	size_t bytesAllocated;
	size_t nextGC;
	Obj *objects;
	
	int grayCount;
	int grayCapacity;
	Obj **grayStack;
} VM;

typedef enum
{
	INTERPRET_OK,
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void runtimeError(const char *format, ...);
bool callValue(Value callee, int argCount);
void defineNativeFn(
	const char *name, NativeFn function, int arity);
void defineNativeVar(const char *name);
bool isFalsey(Value value);
void initVM(bool import_mode);
void freeVM();
InterpretResult interpret(const char *path, const char *source, bool repl_mode);
void push(Value value);
Value pop();

#endif