#ifndef brace_object_h
#define brace_object_h

#include "common.h"
#include "value.h"
#include "chunk.h"
#include "table.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)
#define IS_CLASS(value) isObjType(value, OBJ_CLASS)
#define IS_CLOSURE(value) isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_INSTANCE(value) isObjType(value, OBJ_INSTANCE)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)
#define IS_BOUND_N_M(value) isObjType(value, OBJ_BOUND_N_M)
#define IS_STRING(value) isObjType(value, OBJ_STRING)
#define IS_ARRAY(value) isObjType(value, OBJ_ARRAY)
#define IS_DATA_TYPE(value) isObjType(value, OBJ_DATA_TYPE)
#define IS_MODULE(value) isObjType(value, OBJ_MODULE)

#define AS_BOUND_METHOD(value) ((ObjBoundMethod *)AS_OBJ(value))
#define AS_CLASS(value) ((ObjClass *)AS_OBJ(value))
#define AS_CLOSURE(value) ((ObjClosure *)AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction *)AS_OBJ(value))
#define AS_INSTANCE(value) ((ObjInstance *)AS_OBJ(value))
#define AS_NATIVE(value) (((ObjNative *)AS_OBJ(value)))//->function)
#define AS_BOUND_N_M(value) (((ObjBoundNativeMethod *)AS_OBJ(value)))//->function)
#define AS_STRING(value) ((ObjString *)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString *)AS_OBJ(value))->chars)
#define AS_ARRAY(value) ((ObjArray *)AS_OBJ(value))
#define AS_DATA_TYPE(value) ((ObjDataType *)AS_OBJ(value))
#define AS_MODULE(value) ((ObjModule *)AS_OBJ(value))

typedef enum
{
	OBJ_ARRAY,
	OBJ_BOUND_METHOD,
	OBJ_CLASS,
	OBJ_CLOSURE,
	OBJ_FUNCTION,
	OBJ_INSTANCE,
	OBJ_NATIVE,
	OBJ_BOUND_N_M,
	OBJ_STRING,
	OBJ_UPVALUE,
	OBJ_DATA_TYPE,
	OBJ_MODULE
} ObjType;

struct Obj
{
	ObjType type;
	bool isMarked;
	struct Obj* next;
};

typedef Value (*NativeFn)(int argCount, Value *args);

typedef struct
{
	Obj obj;
	ObjString *name;
	Table methods;
	Table fields;
	Table fieldsTypes;
} ObjClass;

typedef struct
{
	Obj obj;
	bool isAny;
	bool invalid;
	ValueType valueType;
	ObjType objType;
	ObjClass classType;
} ObjDataType;

typedef struct
{
	Obj obj;
	NativeFn function;
	int arity;
	ObjString *name;
} ObjNative;

typedef struct
{
	Obj obj;
	ObjNative *native;
	Value receiver;
} ObjBoundNativeMethod;

typedef enum
{
	TYPE_FUNCTION,
	TYPE_INITIALIZER,
	TYPE_METHOD,
	TYPE_SCRIPT
} FunctionType;

typedef struct
{
	Obj obj;
	// FunctionType type;
	int arity;
	ValueArray argTypes;
	ObjDataType returnType;
	int upvalueCount;
	Chunk chunk;
	ObjString *name;
} ObjFunction;

struct ObjString
{
	Obj obj;
	int length;
	char *chars;
	uint32_t hash;
};

typedef struct ObjUpvalue
{
	Obj obj;
	Value *location;
	Value closed;
	struct ObjUpvalue *next;
} ObjUpvalue;

typedef struct
{
	Obj obj;
	ObjFunction *function;
	ObjUpvalue **upvalues;
	int upvalueCount;
} ObjClosure;

typedef struct
{
  Obj obj;
  ObjClass* klass;
  Table fields; 
  Table fieldsTypes; 
} ObjInstance;

typedef struct
{
  Obj obj;
  // the calling instance is stored
  // for the 'this' variable
  Value receiver;
  ObjClosure* method;
} ObjBoundMethod;

typedef struct
{
	Obj obj;
	ValueArray array;
} ObjArray;

typedef struct
{
	Obj obj;
	ObjString name;
	ObjString path;
	Table fields;
	Table fieldsTypes;
} ObjModule;

ObjBoundMethod *newBoundMethod(Value receiver, ObjClosure *method);
ObjBoundNativeMethod *newBoundNativeMethod(Value receiver, ObjNative *native);
ObjClass *newClass(ObjString *name);
ObjClosure *newClosure(ObjFunction *function);
ObjFunction *newFunction();
ObjInstance *newInstance(ObjClass *klass);
ObjNative *newNative(NativeFn function, int arity, const char *name);
ObjUpvalue *newUpvalue(Value *slot);
// ObjArray *newArray(Value *items, int length);
ObjArray *newArray();
ObjDataType *newDataType(Value value, bool isAny);
Value callDataType(ObjDataType *callee, int argCount, Value *args);
ObjDataType *dataTypeFromString(const char *str);
ObjModule *newModule(const char *name, const char *path);
ObjString *takeString(char *chars, int length);
ObjString *copyString(const char *chars, int length);
char *objectToString(Value value);
void printObject(Value value);
// checks wether the given Value is of ObjType type
static inline bool isObjType(Value value, ObjType type)
{
	return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif