#ifndef brace_value_h
#define brace_value_h

#include "common.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;

typedef enum
{
    VAL_BOOL,
    VAL_NULL,
    VAL_NUMBER,
    VAL_TYPE,
    VAL_OBJ,
} ValueType;

typedef struct
{
    ValueType type;
    union
    {
        bool boolean;
        double number;
        ValueType type;
        Obj* obj;
    } as;
} Value;

// check if a Value contains the given type

#define IS_BOOL(value) ((value).type == VAL_BOOL)
#define IS_NULL(value) ((value).type == VAL_NULL)
#define IS_NUMBER(value) ((value).type == VAL_NUMBER)
#define IS_TYPE(value) ((value).type == VAL_TYPE)
#define IS_OBJ(value) ((value).type == VAL_OBJ)

// produce value from Value

#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)
#define AS_TYPE(value) ((value).as.type)
#define AS_OBJ(value) ((value).as.obj)

// produce Value from value

#define BOOL_VAL(value) ((Value){VAL_BOOL, {.boolean = value}})
#define NULL_VAL ((Value){VAL_NULL, {.number = 0}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = value}})
#define TYPE_VAL(value) ((Value){VAL_TYPE, {.type = value}})
#define OBJ_VAL(object) ((Value){VAL_OBJ, {.obj = (Obj *)object}})

typedef struct
{
    int capacity;
    int count;
    Value *values;
} ValueArray;

bool valuesEqual(Value a, Value b);
void initValueArray(ValueArray *array);
void writeValueArray(ValueArray *array, Value value);
void setValueArray(ValueArray *array, int index, Value value);
void freeValueArray(ValueArray *array);
char *valueToString(Value value);
void printValue(Value value);
// char* valueToCString(Value value);

#endif