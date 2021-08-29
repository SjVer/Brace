#ifdef __INTELLISENSE__
#pragma diag_suppress 29
#pragma diag_suppress 254
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "mem.h"
#include "object.h"
#include "vm.h"
#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif
#include "compiler.h"

#define GC_HEAP_GROW_FACTOR 2

// ------------------- GC ---------------------
static void freeObject(Obj *object);
void freeObjects();

void markObject(Obj *object)
{
    if (object == NULL)
        return;
    if (object->isMarked)
        return;
    #ifdef DEBUG_LOG_GC
        printf("%p mark ", (void *)object);
        printValue(OBJ_VAL(object));
        printf("\n");
    #endif
    object->isMarked = true;

    if (vm.grayCapacity < vm.grayCount + 1)
    {
        vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
        vm.grayStack = (Obj **)realloc(vm.grayStack, sizeof(Obj *) * vm.grayCapacity);

        // allocation failed
        if (vm.grayStack == NULL)
            exit(1);
    }

    vm.grayStack[vm.grayCount++] = object;
}

void markValue(Value value)
{
    if (IS_OBJ(value))
        markObject(AS_OBJ(value));
}

static void markArray(ValueArray *array)
{
    for (int i = 0; i < array->count; i++)
    {
        markValue(array->values[i]);
    }
}

static void blackenObject(Obj *object)
{
    #ifdef DEBUG_LOG_GC
        printf("%p blacken ", (void *)object);
        printValue(OBJ_VAL(object));
        printf("\n");
    #endif
    switch (object->type)
    {
    case OBJ_BOUND_METHOD:
    {
        ObjBoundMethod *bound = (ObjBoundMethod *)object;
        markValue(bound->receiver);
        markObject((Obj *)bound->method);
        break;
    }
    case OBJ_CLASS:
    {
        ObjClass *klass = (ObjClass *)object;
        markTable(&klass->methods);
        markObject((Obj *)klass->name);
        break;
    }
    case OBJ_CLOSURE:
    {
        ObjClosure *closure = (ObjClosure *)object;
        markObject((Obj *)closure->function);
        for (int i = 0; i < closure->upvalueCount; i++)
        {
            markObject((Obj *)closure->upvalues[i]);
        }
        break;
    }
    case OBJ_UPVALUE:
    {
        markValue(((ObjUpvalue *)object)->closed);
        break;
    }
    case OBJ_FUNCTION:
    {
        ObjFunction *function = (ObjFunction *)object;
        markObject((Obj *)function->name);
        markArray(&function->chunk.constants);
        break;
    }
    case OBJ_INSTANCE:
    {
        ObjInstance *instance = (ObjInstance *)object;
        markObject((Obj *)instance->klass);
        markTable(&instance->fields);
        break;
    }
    case OBJ_ARRAY:
    {
        ObjArray *array = (ObjArray *)object;
        markArray(&array->array);
        break;
    }
    case OBJ_BOUND_N_M:
    {
        ObjBoundNativeMethod *method = (ObjBoundNativeMethod *)object;
        markValue(method->receiver);
        break;
    }

    case OBJ_DATA_TYPE:
    case OBJ_NATIVE:
    case OBJ_STRING:
        break;
    }
}

static void markRoots()
{
    // mark stack items
    for (Value *slot = vm.stack; slot < vm.stackTop; slot++)
    {
        markValue(*slot);
    }

    // mark closed-over variables in closures
    for (int i = 0; i < vm.frameCount; i++)
    {
        markObject((Obj *)vm.frames[i].closure);
    }

    // mark upvalues
    for (ObjUpvalue *upvalue = vm.openUpvalues;
         upvalue != NULL;
         upvalue = upvalue->next)
    {
        markObject((Obj *)upvalue);
    }

    // mark globals
    markTable(&vm.globals);
    markObject((Obj *)vm.initString);

    markCompilerRoots();
}

static void traceReferences()
{
    while (vm.grayCount > 0)
    {
        Obj *object = vm.grayStack[--vm.grayCount];
        blackenObject(object);
    }
}

static void sweep()
{
    Obj *previous = NULL;
    Obj *object = vm.objects;
    while (object != NULL)
    {
        if (object->isMarked)
        {
            object->isMarked = false;
            previous = object;
            object = object->next;
        }
        else
        {
            Obj *unreached = object;
            object = object->next;
            if (previous != NULL)
            {
                previous->next = object;
            }
            else
            {
                vm.objects = object;
            }

            freeObject(unreached);
        }
    }
}

// collects and frees all garbage
void collectGarbage()
{
    #ifdef DEBUG_LOG_GC
        printf("-- gc begin\n");
        size_t before = vm.bytesAllocated;
    #endif

    markRoots();
    traceReferences();
    tableRemoveWhite(&vm.strings); // clear unused strings first
                                   // bc they are referenced by
                                   // the objects sweep() clears
    sweep();

    vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

    #ifdef DEBUG_LOG_GC
        printf("-- gc end\n");
        printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
               before - vm.bytesAllocated, before, vm.bytesAllocated,
               vm.nextGC);
    #endif
}

// ----------------------------------------------

// reallocates memory of size newSize for pointer
// if newSize is 0, pointer is freed
void *reallocate(void *pointer, size_t oldSize, size_t newSize)
{
    vm.bytesAllocated += newSize - oldSize;
    if (newSize > oldSize)
    {
    #ifdef DEBUG_STRESS_GC
        collectGarbage();
    #endif
    }

    if (vm.bytesAllocated > vm.nextGC)
    {
        collectGarbage();
    }

    if (newSize == 0)
    {
        free(pointer);
        return NULL;
    }

    void *result = realloc(pointer, newSize);
    if (result == NULL)
    {
        // exit if we have no more memory available
        printf("REALLOCATION FAILED\n");
        exit(1);
    }
    return result;
}

// frees a single object
static void freeObject(Obj *object)
{
#ifdef DEBUG_LOG_GC
    printf(" -- %p free type %d\n", (void *)object, object->type);
#endif

    switch (object->type)
    {
    case OBJ_BOUND_METHOD:
    {
        FREE(ObjBoundMethod, object);
        break;
    }
    case OBJ_CLASS:
    {
        ObjClass *klass = (ObjClass *)object;
        freeTable(&klass->methods);
        FREE(ObjClass, object);
        break;
    }
    case OBJ_CLOSURE:
    {
        ObjClosure *closure = (ObjClosure *)object;
        FREE_ARRAY(ObjUpvalue *, closure->upvalues, closure->upvalueCount);
        FREE(ObjClosure, object);
        break;
    }
    case OBJ_STRING:
    {
        ObjString *string = (ObjString *)object;
        FREE_ARRAY(char, string->chars, string->length + 1);
        FREE(ObjString, object);
        break;
    }
    case OBJ_FUNCTION:
    {
        ObjFunction *function = (ObjFunction *)object;
        freeChunk(&function->chunk);
        FREE(ObjFunction, object);
        break;
    }
    case OBJ_INSTANCE:
    {
        ObjInstance *instance = (ObjInstance *)object;
        freeTable(&instance->fields);
        FREE(ObjInstance, object);
        break;
    }
    case OBJ_NATIVE:
    {
        FREE(ObjNative, object);
        break;
    }
    case OBJ_BOUND_N_M:
    {
        FREE(ObjBoundNativeMethod, object);
        break;
    }
    case OBJ_UPVALUE:
    {
        FREE(ObjUpvalue, object);
        break;
    }
    case OBJ_ARRAY:
    {
        ObjArray *array = (ObjArray *)object;
        freeValueArray(&array->array);
        FREE(ObjArray, object);
        break;
    }
    case OBJ_DATA_TYPE:
    {
        FREE(ObjDataType, object);
        break;
    }
    }
}

// frees the VM's objects from memory
void freeObjects()
{
    Obj *object = vm.objects;
    while (object != NULL)
    {
        Obj *next = object->next;
        freeObject(object);
        object = next;
    }
    free(vm.grayStack);
}

char *formatString(const char *format, ...)
{
    va_list args;
    va_start(args, format);

    int smallSize = sizeof(char) * 1024;
    char *smallBuffer = malloc(smallSize);
    
    int size = vsnprintf(smallBuffer, smallSize, format, args);

    va_end(args);
    
    if (size < sizeof smallBuffer)
        return smallBuffer;

    int bigSize = sizeof(char) * (size + 1);
    char *buffer = malloc(bigSize);

    va_start(args, format);
    vsnprintf(buffer, bigSize, format, args);
    va_end(args);
    
    return buffer;
}