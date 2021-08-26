// disable "type name is not allowed" error
#ifdef __INTELLISENSE__
#pragma diag_suppress 254
#pragma diag_suppress 29
#endif

#include <stdio.h>
#include <string.h>

#include "mem.h"
#include "object.h"
#include "value.h"
#include "table.h"
#include "vm.h"

// allocates an Obj (basically the __init__ for an obj)
#define ALLOCATE_OBJ(type, objectType) \
	(type *)allocateObject(sizeof(type), objectType)

// helper for ALLOCATE_OBJ
static Obj *allocateObject(size_t size, ObjType type)
{
	Obj *object = (Obj *)reallocate(NULL, 0, size);
	object->type = type;
	object->isMarked = false;

	object->next = vm.objects;
	vm.objects = object;

#ifdef DEBUG_LOG_GC
	printf(" -- %p allocate %zu for %d\n", (void *)object, size, type);
#endif

	return object;
}



// allocates and returns a new bound method
ObjBoundMethod *newBoundMethod(Value receiver, ObjClosure *method)
{
	ObjBoundMethod *bound = ALLOCATE_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
	bound->receiver = receiver;
	bound->method = method;
	return bound;
}



// allocates and returns a new class
ObjClass *newClass(ObjString *name)
{
	ObjClass *klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
	klass->name = name;
	initTable(&klass->methods);
	return klass;
}



// allocates and returns a new closure
ObjClosure *newClosure(ObjFunction *function)
{
	ObjUpvalue **upvalues = ALLOCATE(ObjUpvalue *,  function->upvalueCount);
	for (int i = 0; i < function->upvalueCount; i++)
	{
		upvalues[i] = NULL;
	}

	ObjClosure *closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
	closure->function = function;
	closure->upvalues = upvalues;
	closure->upvalueCount = function->upvalueCount;
	return closure;
}



// allocates and returns a new native function
ObjNative *newNative(NativeFn function, int arity, const char *name)
{
	ObjNative *native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
	native->function = function;
	native->arity = arity;
	native->name = copyString(name, strlen(name));
	return native;
}

// allocates and returns a new ObjFunction
ObjFunction *newFunction()
{
	ObjFunction *function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
	function->arity = 0;
	function->upvalueCount = 0;
	function->name = NULL;
	initChunk(&function->chunk);
	return function;
}



ObjInstance *newInstance(ObjClass *klass)
{
	ObjInstance *instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
	instance->klass = klass;
	initTable(&instance->fields);
	return instance;
}



ObjUpvalue *newUpvalue(Value *slot)
{
	ObjUpvalue *upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
	upvalue->closed = NULL_VAL;
	upvalue->location = slot;
	upvalue->next = NULL;
	return upvalue;
}



// ObjArray *newArray(Value *items, int length)
ObjArray *newArray()
{
	ObjArray *array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY);
	initValueArray(&array->array);

	// // copy values over
	// for (int i = 0; i < length; i++)
	// 	writeValueArray(&array->items, items[i]);

	return array;
}



// allocates a ObjString
static ObjString *allocateString(char *chars, int length, uint32_t hash)
{
	ObjString *string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
	string->length = length;
	string->chars = chars;
	string->hash = hash;

	push(OBJ_VAL(string)); // keep string safe from GC
	tableSet(&vm.strings, string, NULL_VAL);
	pop();
	return string;
}

static uint32_t hashString(const char *key, int length)
{
	uint32_t hash = 2166136261u;
	for (int i = 0; i < length; i++)
	{
		hash ^= (uint8_t)key[i];
		hash *= 16777619;
	}
	return hash;
}

ObjString *takeString(char *chars, int length)
{
	uint32_t hash = hashString(chars, length);

	// check if the string already existed
	ObjString *interned = tableFindString(
		&vm.strings, chars, length, hash);
	if (interned != NULL)
	{
		FREE_ARRAY(char, chars, length + 1);
		return interned;
	}

	return allocateString(chars, length, hash);
}

// copies a c string to a ObjString and returns that
ObjString *copyString(const char *chars, int length)
{
	char *heapChars = ALLOCATE(char, length + 1);
	memcpy(heapChars, chars, length);
	heapChars[length] = '\0';
	uint32_t hash = hashString(chars, length);
	
	// check if the string already existed
	ObjString *interned = tableFindString(
		&vm.strings, chars, length, hash);
	if (interned != NULL) return interned;

	return allocateString(heapChars, length, hash);
}



static char *functionToString(ObjFunction *function)
// static void printFunction(ObjFunction *function)
{
	if (function->name == NULL)
		return "<script>";
	return formatString("<Fun %s>", function->name->chars);
}

static char *arrayToString(ObjArray *array)
{
	char *ret = "[";
	for (int i = 0; i < array->array.count; i++)
	{
		ret = formatString("%s%s%s",
			ret,
			IS_STRING(array->array.values[i]) ? 
				formatString("\"%s\"", valueToString(array->array.values[i]))
				: valueToString(array->array.values[i]),
			i + 1 != array->array.count ? ", " : "");
	}
	return formatString("%s]", ret);
}

char *objectToString(Value value)
{
	switch (OBJ_TYPE(value))
	{
	case OBJ_BOUND_METHOD:
		return formatString("<method %s of instance %s>",
			AS_BOUND_METHOD(value)->method->function->name->chars,
			AS_INSTANCE(AS_BOUND_METHOD(value)->receiver)->klass->name->chars);
	
	case OBJ_CLASS:
		return formatString("<Cls %s>", AS_CLASS(value)->name->chars);
	
	case OBJ_CLOSURE:
		return functionToString(AS_CLOSURE(value)->function);

	case OBJ_FUNCTION:
		return functionToString(AS_FUNCTION(value));
	
	case OBJ_INSTANCE:
		return formatString("<%s instance>", AS_INSTANCE(value)->klass->name->chars);
	
	case OBJ_NATIVE:
		return formatString("<native Fun %s>", AS_NATIVE(value)->name->chars);

	case OBJ_STRING:
		return AS_CSTRING(value);
	
	case OBJ_UPVALUE:
		return "<upvalue>";

	case OBJ_ARRAY:
		return arrayToString(AS_ARRAY(value));
	}
	return "<OBJ-TO-STRING-ERROR>";
}

// prints an Obj
void printObject(Value value)
{
	// switch (OBJ_TYPE(value))
	// {
	// case OBJ_BOUND_METHOD:
	// 	// printFunction(AS_BOUND_METHOD(value)->method->function);
	// 	printf("<method %s of %s instance>",
	// 		AS_BOUND_METHOD(value)->method->function->name->chars,
	// 		AS_INSTANCE(AS_BOUND_METHOD(value)->receiver)->klass->name->chars);
	// 	break;
	// case OBJ_CLASS:
	// 	printf("<class %s>", AS_CLASS(value)->name->chars);
	// 	break;
	// case OBJ_CLOSURE:
	// 	printFunction(AS_CLOSURE(value)->function);
	// 	break;
	// case OBJ_FUNCTION:
	// 	printFunction(AS_FUNCTION(value));
	// 	break;
	// case OBJ_INSTANCE:
	// 	printf("<%s instance>", AS_INSTANCE(value)->klass->name->chars);
	// 	break;
	// case OBJ_NATIVE:
	// 	printf("<native function>");
	// 	break;
	// case OBJ_STRING:
	// 	printf("%s", AS_CSTRING(value));
	// 	break;
	// case OBJ_UPVALUE:
	// 	printf("<upvalue>");
	// 	break;
	// case OBJ_ARRAY:
	// 	printf("[");
	// 	// Value *item = AS_ARRAY(value)->items.values;
	// 	for (int i = 0; i < AS_ARRAY(value)->length; i++)
	// 	{
	// 		printValue(AS_ARRAY(value)->items.values[i]);
	// 		printf(i == AS_ARRAY(value)->length - 1 ? "" : ", ");
	// 	}
	// 	printf("]");
	// 	break;
	// }
	printf("%s", objectToString(value));
}