#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "natives.h"
#include "common.h"
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

Value helpNative(int argCount, Value *args)
{
    printf(HELP_MESSAGE);
    return NULL_VAL;
} 

// ------- NATIVES -----------
static Value clockNative(int argCount, Value *args)
{
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static Value clearNative(int argCount, Value *args)
{
    system("@cls||clear");
    return NULL_VAL;
}

static Value sleepNative(int argCount, Value *args)
{
    if (!checkArg(args[0], VAL_NUMBER, 0))
        return nativeRuntimeError("Expected an argument of type number");

    sleep(AS_NUMBER(args[0]));
    return NULL_VAL;
}

static Value typeNative(int argCount, Value *args)
{
    if (IS_INSTANCE(args[0]))
        return OBJ_VAL(AS_INSTANCE(args[0])->klass);
    return OBJ_VAL(newDataType(args[0], false));
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

// yoinked from https://github.com/valkarias
static Value inputNative(int argCount, Value *args)
{
    if (argCount != 0 && argCount != 1)
        return nativeRuntimeError("Expect 0 or 1 arguments.");

    if (argCount != 0)
    {
        if (!checkArg(args[0], VAL_OBJ, OBJ_STRING))
            return nativeRuntimeError("Expected string argument.");

        printf("%s", AS_CSTRING(args[0]));
    }

    uint64_t currentSize = 128;
    char *line = ALLOCATE(char, currentSize);

    if (line == NULL)
    {
        runtimeError("A Memory error occured on input()!?");
        return NULL_VAL;
    }

    int c = EOF;
    uint64_t length = 0;
    while ((c = getchar()) != '\n' && c != EOF)
    {
        line[length++] = (char)c;

        if (length + 1 == currentSize)
        {
            int oldSize = currentSize;
            currentSize = GROW_CAPACITY(currentSize);
            line = GROW_ARRAY(char, line, oldSize, currentSize);

            if (line == NULL)
            {
                printf("Unable to allocate more memory\n");
                exit(71);
            }
        }
    }

    if (length != currentSize)
    {
        line = GROW_ARRAY(char, line, currentSize, length + 1);
    }

    line[length] = '\0';

    return OBJ_VAL(takeString(line, length));
}
// ---------------------------

void defineNatives()
{
    // functions
    defineNativeFn("Clock",    clockNative, 0);
    defineNativeFn("Clear",    clearNative, 0);
    defineNativeFn("Sleep",    sleepNative, 1);
    defineNativeFn("GetInput", inputNative, -1);   
    defineNativeFn("TypeOf",   typeNative,  1);
    defineNativeFn("Str",      strNative,   1);
    defineNativeFn("Bln",      boolNative,  1);
}