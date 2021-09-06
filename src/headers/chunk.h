#ifndef brace_chunk_h
#define brace_chunk_h

#include "common.h"
#include "value.h"

typedef enum
{
	OP_CONSTANT,
	OP_NULL,
	OP_TRUE,
	OP_FALSE,
	OP_POP,
	OP_DUPLICATE,
	OP_GET_TYPE,
	OP_ASSERT_TYPE,
	OP_TERNARY,
	OP_GET_LOCAL,
	OP_SET_LOCAL,
	OP_GET_GLOBAL,
	OP_DEFINE_GLOBAL,
	OP_SET_GLOBAL,
	OP_GET_NVAR,
	OP_SET_NVAR,
	OP_UPDATE_LAST,
	OP_GET_UPVALUE,
	OP_SET_UPVALUE,
	OP_DEFINE_FIELD,
	OP_GET_PROPERTY,
	OP_SET_PROPERTY,
	OP_GET_SUPER,
	OP_GET_INDEX,
	OP_SET_INDEX,
	OP_ARRAY_LENGTH,
	OP_ARRAY,
	OP_EQUAL,
	OP_GREATER,
	OP_LESS,
	OP_ADD,
	OP_INCREMENT,
	OP_SUBTRACT,
	OP_DECREMENT,
	OP_MULTIPLY,
	OP_DIVIDE,
	OP_MODULO,
	OP_NEGATE,
	OP_NOT,
	OP_PRINT,
	OP_PRINT_LN,
	OP_JUMP,
	OP_JUMP_IF_FALSE,
	OP_JUMP_BACK,
	OP_CALL,
	OP_INVOKE,
	OP_SUPER_INVOKE,
	OP_CLOSURE,
	OP_CLOSE_UPVALUE,
	OP_CLASS,
	OP_INHERIT,
	OP_METHOD,
	OP_IMPORT,
	OP_EXIT,
	OP_RETURN,
	OP_SCRIPT_END,
} OpCode;

typedef struct
{
	int count;
	int capacity;
	uint8_t *code;
	int *lines;
	ValueArray constants;
} Chunk;

void initChunk(Chunk *chunk);
void writeChunk(Chunk *chunk, uint8_t byte, int line);
void freeChunk(Chunk *chunk);
int addConstant(Chunk *chunk, Value value);

#endif
