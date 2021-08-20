#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "natives.h"
#include "mem.h"
#include "vm.h"
#include "value.h"

static bool checkArg(Value arg, int valueType, int objType)
{
    if (arg.type != valueType)
        return false;
    if (valueType == VAL_OBJ && arg.as.obj->type != objType)
        return false;
    return true;
}

static Value nativeRuntimeError(char *msg)
{
    runtimeError(msg);
    return (Value){-1};
}

// ------- NATIVES -----------
static Value clockNative(int argCount, Value *args)
{
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static Value clearNative(int argCount, Value *args)
{
    system("@cls||clear");
    return NIL_VAL;
}

static Value sleepNative(int argCount, Value *args)
{
    if (!checkArg(args[0], VAL_NUMBER, 0))
        return nativeRuntimeError("Expected an argument of type number");

    sleep(AS_NUMBER(args[0]));
    return NIL_VAL;
}

static Value strNative(int argCount, Value *args)
{
    char *str = valueToString(args[0]);
    return OBJ_VAL(copyString(str, strlen(str)));
}

static Value boolNative(int argCount, Value *args)
{
    return BOOL_VAL(!isFalsey(args[0]));
}

static Value arrayNative(int argCount, Value *args)
{
    if (!checkArg(args[0], VAL_OBJ, OBJ_STRING))
        return nativeRuntimeError("Expect an argument of type string");
    
    char *string = AS_CSTRING(args[0]);
    ObjArray *array = newArray();

    for (int i = 0; i < strlen(string); i++)
        writeValueArray(&array->array, OBJ_VAL(
            copyString(formatString("%c", string[i]), 2)));

    return OBJ_VAL(array);
}
// ---------------------------

void defineNatives()
{
    defineNative("clock", clockNative, 0);
    defineNative("clear", clearNative, 0);
    defineNative("sleep", sleepNative, 1);
    defineNative("str",   strNative,   1);
    defineNative("bool",  boolNative,  1);
    defineNative("array", arrayNative, 1);
}