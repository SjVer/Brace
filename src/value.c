#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem.h"
#include "value.h"
#include "object.h"

void initValueArray(ValueArray *array)
{
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void writeValueArray(ValueArray *array, Value value)
{
    if (array->capacity < array->count + 1)
    {
        // array needs to grow first
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(Value, array->values,
                                   oldCapacity, array->capacity);
    }

    // add value to array and increment count
    array->values[array->count] = value;
    array->count++;
}

void freeValueArray(ValueArray *array)
{
    FREE_ARRAY(Value, array->values, array->capacity);
    initValueArray(array);
}

char *valueToString(Value value)
{
    switch (value.type)
    {
    case VAL_NIL:
        return "nil";
    case VAL_BOOL:
        return AS_BOOL(value) ? "true" : "false";
    case VAL_NUMBER:
        return formatString("%g", AS_NUMBER(value));
    case VAL_OBJ:
        return objectToString(value);
    }
    return "<VALUE-TO-STRING-ERROR>";
}

void printValue(Value value)
{
    // switch (value.type)
    // {
    // case VAL_BOOL:
    //     printf(AS_BOOL(value) ? "true" : "false");
    //     break;
    // case VAL_NIL:
    //     printf("nil");
    //     break;
    // case VAL_NUMBER:
    //     printf("%g", AS_NUMBER(value));
    //     break;
    // case VAL_OBJ:
    //     printObject(value);
    //     break;
    // }
    printf("%s", valueToString(value));
}

bool valuesEqual(Value a, Value b)
{
    if (a.type != b.type)
        return false;
    switch (a.type)
    {
    case VAL_BOOL:
        return AS_BOOL(a) == AS_BOOL(b);
    case VAL_NIL:
        return true;
    case VAL_NUMBER:
        return AS_NUMBER(a) == AS_NUMBER(b);
    case VAL_OBJ:
    {
        return AS_OBJ(a) == AS_OBJ(b);
    }
    default:
        return false; // Unreachable.
    }
}