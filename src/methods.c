#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "methods.h"
#include "value.h"
#include "common.h"
#include "object.h"
#include "mem.h"
#include "vm.h"

#define self (args[-1])

// HELPER FUNCTIONS

static bool checkArg(Value arg, int valueType, int objType)
{
    if (arg.type != valueType)
        return false;
    if (valueType == VAL_OBJ && arg.as.obj->type != objType)
        return false;
    return true;
}

static Value methodRuntimeError(char *msg)
{
    runtimeError(msg);
    return (Value){-1};
}

// ============= VALUE METHODS =============

// NUMBER
static Value numberMethod_IsInt(int argCount, Value *args)
{
    return BOOL_VAL((int)AS_NUMBER(self) == AS_NUMBER(self));
}
static Value numberMethod_ToHex(int argCount, Value *args)
{
    // check int
    if (!((int)AS_NUMBER(self) == AS_NUMBER(self)))
        return methodRuntimeError(formatString(
            "Expect an integer, not '%s'.", valueToString(self)
        ));

    char *buf = formatString("0x%x", (int)AS_NUMBER(self));
    return OBJ_VAL(copyString(buf, strlen(buf)));
}

// ============= OBJECT METHODS =============

// STRING
static Value stringMethod_ToNum(int argCount, Value *args)
{
    char *cropped;
    double number = strtod(AS_CSTRING(self), &cropped);
    return NUMBER_VAL(number);
}
static Value stringMethod_Split(int argCount, Value *args)
{
    char *string = AS_CSTRING(self);
    ObjArray *array = newArray();

    for (int i = 0; i < strlen(string); i++)
        writeValueArray(&array->array, OBJ_VAL(
            copyString(formatString("%c", string[i]), 2)));

    return OBJ_VAL(array);
}
static Value stringMethod_Replace(int argCount, Value *args)
{
    // check the two args
    if (!checkArg(args[0], VAL_OBJ, OBJ_STRING) ||
        !checkArg(args[1], VAL_OBJ, OBJ_STRING))
        return methodRuntimeError(formatString(
            "Expected two string arguments, not '%s' and '%s'.",
            valueToString(args[0]), valueToString(args[1])
        ));

    char *orig = AS_CSTRING(self);
    char *rep = AS_CSTRING(args[0]);
    char *with = AS_CSTRING(args[1]);

    // source: https://stackoverflow.com/questions/779875/what-function-is-to-replace-a-substring-from-a-string-in-c
    // answer by: https://stackoverflow.com/users/44065/jmucchiello

    char *result;  // the return string
    char *ins;     // the next insert point
    char *tmp;     // varies
    int len_rep;   // length of rep (the string to remove)
    int len_with;  // length of with (the string to replace rep with)
    int len_front; // distance between rep and end of last rep
    int count;     // number of replacements

    // sanity checks and initialization
    if (!orig || !rep)
        return OBJ_VAL(copyString(orig, strlen(orig)));
    len_rep = strlen(rep);
    if (len_rep == 0)
        return OBJ_VAL(copyString(orig, strlen(orig))); // empty rep causes infinite loop during count
    if (!with)
        with = "";
    len_with = strlen(with);

    // count the number of replacements needed
    ins = orig;
    for (count = 0; (tmp = strstr(ins, rep)); ++count)
    {
        ins = tmp + len_rep;
    }

    tmp = result = malloc(strlen(orig) + (len_with - len_rep) * count + 1);

    if (!result)
        return OBJ_VAL(copyString(orig, strlen(orig)));

    // first time through the loop, all the variable are set correctly
    // from here on,
    //    tmp points to the end of the result string
    //    ins points to the next occurrence of rep in orig
    //    orig points to the remainder of orig after "end of rep"
    while (count--)
    {
        ins = strstr(orig, rep);
        len_front = ins - orig;
        tmp = strncpy(tmp, orig, len_front) + len_front;
        tmp = strcpy(tmp, with) + len_with;
        orig += len_front + len_rep; // move to next "end of rep"
    }
    strcpy(tmp, orig);
    return OBJ_VAL(copyString(result, strlen(result)));
}

// ARRAY
static Value arrayMethod_Length(int argCount, Value *args)
{
    return NUMBER_VAL(AS_ARRAY(self)->array.count);
}
static Value arrayMethod_Clear(int argCount, Value *args)
{
    AS_ARRAY(self)->array.count = 0;
    return self;
}
static Value arrayMethod_Reverse(int argCount, Value *args)
{
    ValueArray *array = &AS_ARRAY(self)->array;
    ValueArray copy;
    initValueArray(&copy);

    // copy array
    for (int i = 0; i < array->count; i++)
        writeValueArray(&copy, array->values[i]);
    
    for (int i = 0; i < array->count; i++)
        array->values[i] = copy.values[copy.count - i - 1];

    return self;
}
static Value arrayMethod_Copy(int argCount, Value *args)
{
    ValueArray oldarray = AS_ARRAY(self)->array;
    ObjArray *newarray = newArray();
    for(int i = 0; i < oldarray.count; i++)
        writeValueArray(&newarray->array, oldarray.values[i]);
    return OBJ_VAL(newarray);
}
static Value arrayMethod_Prepend(int argCount, Value *args)
{
    ValueArray *array = &AS_ARRAY(self)->array;

    if (array->count == 0)
    {
        writeValueArray(array, args[0]);
        return NULL_VAL;
    }

    // first duplicate last item
    writeValueArray(array, array->values[array->count - 1]);
    // then shift all others once
    for (int i = array->count - 2; i >= 0; i--)
        array->values[i + 1] = array->values[i];
    // then set first to new value
    array->values[0] = args[0];
    return self;
}
static Value arrayMethod_Append(int argCount, Value *args)
{
    ValueArray *array = &AS_ARRAY(self)->array;
    writeValueArray(array, args[0]);
    return self;
}
static Value arrayMethod_Insert(int argCount, Value *args)
{
    ValueArray *array = &AS_ARRAY(self)->array;
    // first check if index is valid
    // is num?
    if (!checkArg(args[0], VAL_NUMBER, 0))
        return methodRuntimeError(formatString("Invalid index '%s'.", 
            valueToString(args[0]), array->count));
    // is int?
    if ((int)AS_NUMBER(args[0]) != AS_NUMBER(args[0]))
        return methodRuntimeError(formatString("Index should be an integer, not '%s'.", 
            valueToString(args[0])));

    int index = (int)AS_NUMBER(args[0]);
    if (index < 0)
        index = array->count + index + 1;

    // check if index is in correct range:
    if (index < 0 || index > array->count)
        return methodRuntimeError(formatString("Invalid index '%d' for list of length %d.",
            index, array->count));

    // index is valid!

    // duplicate last item and shift everything before that and after index to the right
    writeValueArray(array, array->values[array->count - 1]);
    for (int i = array->count - 2; i >= index; i--)
        array->values[i + 1] = array->values[i];

    // set new value
    array->values[index] = args[1];
    return self;
}
static Value arrayMethod_Find(int argCount, Value *args)
{
    ValueArray *array = &AS_ARRAY(self)->array;
    for (int i = 0; i < array->count; i++)
        if (valuesEqual(array->values[i], args[0]))
            return NUMBER_VAL(i);
    return BOOL_VAL(false);
}
static Value arrayMethod_Remove(int argCount, Value *args)
{
    ValueArray *array = &AS_ARRAY(self)->array;

    // search array
    int i;
    do {
        for (i=0; i < array->count; i++)
            if (valuesEqual(array->values[i], args[0]))
                break;

        // found?
        if (i < array->count)
        {
            for (int j = i; j < array->count; j++)
                array->values[j] = array->values[j + 1];
            array->count--;
        }
    } while (i != array->count);

    return self;
}
static Value arrayMethod_Pop(int argCount, Value *args)
{
    ValueArray *array = &AS_ARRAY(self)->array;
    Value popped = array->values[array->count - 1];
    array->count--;
    return popped;
}
static Value arrayMethod_Join(int argCount, Value *args)
{
    ValueArray array = AS_ARRAY(self)->array;

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
    createMethod(&numberMethods, "IsInt",  numberMethod_IsInt,  0);
    createMethod(&numberMethods, "ToHex",  numberMethod_ToHex,  0);

    createMethod(&stringMethods, "ToNum",  stringMethod_ToNum,  0);
    createMethod(&stringMethods, "Split",  stringMethod_Split,  0);
    createMethod(&stringMethods, "Replace",stringMethod_Replace,2);
    
    createMethod(&arrayMethods,  "Length", arrayMethod_Length,  0);
    createMethod(&arrayMethods,  "Clear",  arrayMethod_Clear,   0);
    createMethod(&arrayMethods,  "Reverse",arrayMethod_Reverse, 0);
    createMethod(&arrayMethods,  "Copy",   arrayMethod_Copy,    0);
    createMethod(&arrayMethods,  "Prepend",arrayMethod_Prepend, 1);
    createMethod(&arrayMethods,  "Append", arrayMethod_Append,  1);
    createMethod(&arrayMethods,  "Insert", arrayMethod_Insert,  2);
    createMethod(&arrayMethods,  "Find",   arrayMethod_Find,    1);
    createMethod(&arrayMethods,  "Remove", arrayMethod_Remove,  1);
    createMethod(&arrayMethods,  "Pop",    arrayMethod_Pop,     0);
    createMethod(&arrayMethods,  "Join",   arrayMethod_Join,    0);
}