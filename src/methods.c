#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "methods.h"
#include "value.h"
#include "common.h"
#include "object.h"
#include "mem.h"
#include "vm.h"


// HELPER FUNCTIONS

// static bool checkArg(Value arg, int valueType, int objType)
// {
//     if (arg.type != valueType)
//         return false;
//     if (valueType == VAL_OBJ && arg.as.obj->type != objType)
//         return false;
//     return true;
// }

// static Value methodRuntimeError(char *msg)
// {
//     runtimeError(msg);
//     return (Value){-1};
// }

// ============= OBJECT METHODS =============

// STRING
static Value stringMethod_ToNum(int argCount, Value *args)
{
    char *cropped;
    double number = strtod(AS_CSTRING(args[0]), &cropped);
    return NUMBER_VAL(number);
}
static Value stringMethod_Split(int argCount, Value *args)
{
    char *string = AS_CSTRING(args[0]);
    ObjArray *array = newArray();

    for (int i = 0; i < strlen(string); i++)
        writeValueArray(&array->array, OBJ_VAL(
            copyString(formatString("%c", string[i]), 2)));

    return OBJ_VAL(array);
}

// ARRAY
static Value arrayMethod_Append(int argCount, Value *args)
{
    // printf("SELF: ");
    // printValue(args[-1]);
    // printf("\n");

    ValueArray array = AS_ARRAY(args[-1])->array;
    writeValueArray(&array, args[0]);
    return NULL_VAL;
}
static Value arrayMethod_Join(int argCount, Value *args)
{
    ValueArray array = AS_ARRAY(args[0])->array;

    char *ret = "";
    for (int i = 0; i < array.count; i++)
	{
		ret = formatString("%s%s",
			ret, valueToString(array.values[i]));
	}
    return OBJ_VAL(copyString(ret, strlen(ret)));
}


// ============= ============= =============

static void createMethod(ValueArray *array, const char *name, NativeFn method, int arity)
{ writeValueArray(array, OBJ_VAL(newNative(method, arity + 1, name))); }

void defineAllMethods()
{
    createMethod(&stringMethods, "ToNum",  stringMethod_ToNum,  0);
    createMethod(&stringMethods, "Split",  stringMethod_Split,  0);
    
    createMethod(&arrayMethods,  "Append", arrayMethod_Append,  1);
    createMethod(&arrayMethods,  "Join",   arrayMethod_Join,    0);
}