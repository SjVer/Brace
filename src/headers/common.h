#ifndef brace_common_h
#define brace_common_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <cwalk.h>

// compiler name
#ifndef COMPILER
#define COMPILER "gcc"
#endif

// lib path
#ifndef BRACE_LIB_PATH
#error "BRACE_LIB_PATH not defined. \
Add \"-D BRACE_LIB_PATH=\\\"<path>\\\"\" \
to the compiler arguments."
#define BRACE_LIB_PATH "defined by compiler"
#endif

// OS name
#ifdef _WIN32
#define OS "Windows 32-bit"
#elif _WIN64
#define OS "Windows 64-bit"
#elif __APPLE__ || __MACH__
#define OS "Mac OSX"
#elif __linux__
#define OS "Linux"
#elif __FreeBSD__
#define OS "FreeBSD"
#elif __unix || __unix__
#define OS "Unix"
#else
#define OS "Other"
#endif

// version inof
#define BRACE_VERSION_0 1
#define BRACE_VERSION_1 0
#define BRACE_VERSION_2 0

// welcome message
/* "..." = 
    BRACE_VERSION_0,
    BRACE_VERSION_1,
    BRACE_VERSION_2,
    __DATE__, __TIME__,
    COMPILER, __VERSION_,
    OS
*/
#define WELCOME_MESSAGE \
"Brace %d.%d.%d (%s, %s)\n\
[%s %s] on %s\n\
Type \"Clear();\" to clear the screen, \
\"Exit;\" to quit \
or \"Help();\" for more information.\n"

// help message
#define HELP_MESSAGE \
"This is the Brace repl. Here you can run \
commands and test your snippets.\n\
Type \"Clear();\" to clear the screen or \"Exit;\" to quit.\n\
For more information go to \
https://github.com/SjVer/Brace.\n"

// prompts
#define PROMPT_NORM "brc:> "
#define PROMPT_IND  "...   "

// #define DEBUG_TRACE_EXECUTION
// #define DEBUG_PRINT_CODE
#define UINT8_COUNT (UINT8_MAX + 1)

#endif