#ifndef brace_natives_h
#define brace_natives_h

#include "value.h"

Value helpNative();
void defineNatives();

typedef enum
{
    NVAR_NULL,
    NVAR_LAST,
    NVAR_FUN,
    NVAR_SCRIPT
} NativeVarType;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
static char *NativeVars[] = {
    [NVAR_NULL] = "_",
    [NVAR_LAST] = "_LAST",
    [NVAR_FUN] = "_FUN",
    [NVAR_SCRIPT] = "_SCRIPT",
};
#pragma GCC diagnostic pop

#endif // !brace_natives_h