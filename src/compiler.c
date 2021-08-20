#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"
#include "object.h"
#include "mem.h"
#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

// -------- structs n shit --------

typedef struct
{
	Token current;
	Token previous;
	bool hadError;
	bool panicMode;
} Parser;

typedef enum
{
	PREC_NONE,
	PREC_ASSIGNMENT, // =
	PREC_OR,		 // or
	PREC_AND,		 // and
	PREC_EQUALITY,	 // == !=
	PREC_COMPARISON, // < > <= >=
	PREC_TERM,		 // + -
	PREC_FACTOR,	 // * /
	PREC_UNARY,		 // ! -
	PREC_CALL,		 // . ()
	PREC_PRIMARY	 // literals n shit
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct
{
	ParseFn prefix;
	ParseFn infix;
	Precedence precedence;
} ParseRule;

typedef struct
{
	Token name;
	int depth;
	bool isCaptured;
} Local;

typedef struct
{
	uint8_t index;
	bool isLocal;
} Upvalue;

typedef enum
{
	TYPE_FUNCTION,
	TYPE_INITIALIZER,
	TYPE_METHOD,
	TYPE_SCRIPT
} FunctionType;

typedef struct Compiler
{
	struct Compiler *enclosing;
	ObjFunction *function;
	FunctionType type;

	Local locals[UINT8_COUNT];
	int localCount;
	Upvalue upvalues[UINT8_COUNT];
	int scopeDepth;
} Compiler;

typedef struct ClassCompiler
{
	struct ClassCompiler *enclosing;
	bool hasSuperclass;
} ClassCompiler;

// -------- variables --------

// the parser
Parser parser;
// the current compiler
Compiler *current = NULL;
// the current class being compiled
ClassCompiler *currentClass = NULL;
// the chunk being compiled
Chunk *compilingChunk;
// retreives the current chunk
static Chunk *currentChunk()
{
	return &current->function->chunk;
}

// -------- predefinitions --------
static void initCompiler(Compiler *compiler, FunctionType type);
static ObjFunction *endCompiler();
static int addUpvalue(Compiler *compiler, uint8_t index, bool isLocal);

static void advance();
static void declareVariable();
static void beginScope();
static void endScope();

static void expression();
static void statement();
static void varDeclaration();
static void classDeclaration();
static void funDeclaration();
static void expressionStatement();
static void printStatement();
static void returnStatement();
static void exitStatement();
static void ifStatement();
static void forStatement();
static void whileStatement();
static void declaration();

static void grouping(bool canAssign);
static void number(bool canAssign);
static void unary(bool canAssign);
static void binary(bool canAssign);
static void call(bool canAssign);
static void literal(bool canAssign);
static void string(bool canAssign);
static void variable(bool canAssign);
static void and_(bool canAssign);
static void or_(bool canAssign);
static void dot(bool canAssign);
static void this_(bool canAssign);
static void super_(bool canAssign);
static void b_brace(bool canAssign);

static void namedVariable(Token name, bool canAssing);

// -------- error stuff --------

// displays an error with the given token and message
static void errorAt(Token *token, const char *message)
{
	// already in panicmode. swallow error.
	if (parser.panicMode)
		return;

	parser.panicMode = true;

	fprintf(stderr, "[line %d] Error", token->line);

	if (token->type == TOKEN_EOF)
	{
		fprintf(stderr, " at end");
	}
	else if (token->type == TOKEN_ERROR)
	{
		// Nothing.
	}
	else
	{
		fprintf(stderr, " at '%.*s'", token->length, token->start);
	}

	fprintf(stderr, ": %s\n", message);
	parser.hadError = true;
}

// displays an error at the previous token with the given message
static void error(const char *message)
{
	errorAt(&parser.previous, message);
}

// displays an error at the current token with the given message
static void errorAtCurrent(const char *message)
{
	errorAt(&parser.current, message);
}

// skip tokens until something that appears to be the end
// of a statement or smth is reached
static void synchronize()
{
	parser.panicMode = false;

	while (parser.current.type != TOKEN_EOF)
	{
		if (parser.previous.type == TOKEN_SEMICOLON)
			return;
		switch (parser.current.type)
		{
		case TOKEN_CLASS:
		case TOKEN_FUN:
		case TOKEN_VAR:
		case TOKEN_FOR:
		case TOKEN_IF:
		case TOKEN_WHILE:
		case TOKEN_PRINT:
		case TOKEN_RETURN:
			return;

		default:; // Do nothing.
		}

		advance();
	}
}

// -------- token flow stuff --------

// advances to the next token
static void advance()
{
	parser.previous = parser.current;

	for (;;)
	{
		parser.current = scanToken();
		if (parser.current.type != TOKEN_ERROR)
			break;

		errorAtCurrent(parser.current.start);
	}
}

// checks if the current token is of the given type
static bool check(TokenType type)
{
	return parser.current.type == type;
}

// consume the next token if it is of the correct type,
// otherwise throw an error with the given message
static void consume(TokenType type, const char *message)
{
	if (parser.current.type == type)
	{
		advance();
		return;
	}

	errorAtCurrent(message);
}

// returns true and advances if the current token is of the given type
static bool match(TokenType type)
{
	if (!check(type))
		return false;
	advance();
	return true;
}

// -------- emit/byte stuff --------

// write one byte to the current chunk
static void emitByte(uint8_t byte)
{
	writeChunk(currentChunk(), byte, parser.previous.line);
}

// emit two bytes to the current chunk
static void emitBytes(uint8_t byte1, uint8_t byte2)
{
	emitByte(byte1);
	emitByte(byte2);
}

// emit a (partially unfinished) jump instruction
static int emitJump(uint8_t instruction)
{
	emitByte(instruction);
	emitByte(0xff);
	emitByte(0xff);
	return currentChunk()->count - 2;
}

// patch up the last unfinished jump instruction emitted by emitJump()
static void patchJump(int offset)
{
	// -2 to adjust for the bytecode for the jump offset itself.
	int jump = currentChunk()->count - offset - 2;

	if (jump > UINT16_MAX)
	{
		error("Too much code to jump over.");
	}

	currentChunk()->code[offset] = (jump >> 8) & 0xff;
	currentChunk()->code[offset + 1] = jump & 0xff;
}

// emit the return instruction to the current chunk
static void emitReturn()
{
	if (current->type == TYPE_INITIALIZER)
	{
		// init() returns 'this'
		emitBytes(OP_GET_LOCAL, 0);
	}
	else
	{
		emitByte(OP_NIL);
	}

	emitByte(OP_RETURN);
}

// emit a jump back instruction or smth
static void emitLoop(int loopStart)
{
	emitByte(OP_JUMP_BACK);

	int offset = currentChunk()->count - loopStart + 2;
	if (offset > UINT16_MAX)
		error("Loop body too large.");

	emitByte((offset >> 8) & 0xff);
	emitByte(offset & 0xff);
}

// create a constant for the stack and return its index
static uint8_t makeConstant(Value value)
{
	int constant = addConstant(currentChunk(), value);
	if (constant > UINT8_MAX)
	{
		error("Too many constants in one chunk.");
		return 0;
	}

	return (uint8_t)constant;
}

// emit a constant to the current chunk
static void emitConstant(Value value)
{
	emitBytes(OP_CONSTANT, makeConstant(value));
}

// makes an identifier with the given name
static uint8_t identifierConstant(Token *name)
{
	return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

// check if two identifiers are the same
static bool identifiersEqual(Token *a, Token *b)
{
	if (a->length != b->length)
		return false;
	return memcmp(a->start, b->start, a->length) == 0;
}

// returns the local's slot if it exists, else -1
static int resolveLocal(Compiler *compiler, Token *name)
{
	for (int i = compiler->localCount - 1; i >= 0; i--)
	{
		Local *local = &compiler->locals[i];
		if (identifiersEqual(name, &local->name))
		{
			if (local->depth == -1)
			{
				error("Can't read local variable in its own initializer.");
			}
			return i;
		}
	}

	return -1;
}

// returns the index of an upvalue if it exists, else -1
static int resolveUpvalue(Compiler *compiler, Token *name)
{
	if (compiler->enclosing == NULL)
		return -1;

	int local = resolveLocal(compiler->enclosing, name);
	if (local != -1)
	{
		compiler->enclosing->locals[local].isCaptured = true;
		return addUpvalue(compiler, (uint8_t)local, true);
	}

	int upvalue = resolveUpvalue(compiler->enclosing, name);
	if (upvalue != -1)
	{
		return addUpvalue(compiler, (uint8_t)upvalue, false);
	}

	return -1;
}

// adds a local with the given name automatically assigning
// its slot and depth
static void addLocal(Token name)
{
	if (current->localCount == UINT8_COUNT)
	{
		error("Too many local variables in function.");
		return;
	}

	Local *local = &current->locals[current->localCount++];
	local->name = name;
	local->depth = -1; // mark uninitialized
	local->isCaptured = false;
}

// like addLocal() but for upvalues
static int addUpvalue(Compiler *compiler, uint8_t index, bool isLocal)
{
	int upvalueCount = compiler->function->upvalueCount;

	for (int i = 0; i < upvalueCount; i++)
	{
		Upvalue *upvalue = &compiler->upvalues[i];
		if (upvalue->index == index && upvalue->isLocal == isLocal)
		{
			return i;
		}
	}

	if (upvalueCount == UINT8_COUNT)
	{
		error("Too many closure variables in function.");
		return 0;
	}

	compiler->upvalues[upvalueCount].isLocal = isLocal;
	compiler->upvalues[upvalueCount].index = index;
	return compiler->function->upvalueCount++;
}

// create a token with the given text and return it
static Token syntheticToken(const char *text)
{
	Token token;
	token.start = text;
	token.length = (int)strlen(text);
	return token;
}

// -------- grammar stuff --------

// rules for parsing any token
ParseRule rules[] = {
	[TOKEN_LEFT_PAREN]    	= {grouping,call,   PREC_CALL},
	[TOKEN_RIGHT_PAREN] 	= {NULL, 	NULL,   PREC_NONE},
	[TOKEN_LEFT_BRACE] 		= {NULL, 	NULL,   PREC_NONE},
	[TOKEN_RIGHT_BRACE] 	= {NULL, 	NULL,   PREC_NONE},
	[TOKEN_LEFT_B_BRACE] 	= {b_brace, NULL,   PREC_NONE},
	[TOKEN_RIGHT_B_BRACE] 	= {NULL, 	NULL,   PREC_NONE},
	[TOKEN_COMMA] 			= {NULL, 	NULL,   PREC_NONE},
	[TOKEN_DOT] 			= {NULL, 	dot,   	PREC_CALL},
	[TOKEN_MINUS] 			= {unary, 	binary, PREC_TERM},
	[TOKEN_PLUS] 			= {NULL, 	binary, PREC_TERM},
	[TOKEN_SEMICOLON] 		= {NULL, 	NULL,   PREC_NONE},
	[TOKEN_SLASH] 			= {NULL, 	binary, PREC_FACTOR},
	[TOKEN_STAR] 			= {NULL, 	binary, PREC_FACTOR},
	[TOKEN_BANG] 			= {unary, 	NULL,   PREC_NONE},
	[TOKEN_BANG_EQUAL] 		= {NULL, 	binary, PREC_EQUALITY},
	[TOKEN_EQUAL] 			= {NULL, 	NULL,   PREC_NONE},
	[TOKEN_EQUAL_EQUAL] 	= {NULL, 	binary, PREC_EQUALITY},
	[TOKEN_GREATER]			= {NULL, 	binary, PREC_COMPARISON},
	[TOKEN_GREATER_EQUAL] 	= {NULL, 	binary, PREC_COMPARISON},
	[TOKEN_LESS] 			= {NULL, 	binary, PREC_COMPARISON},
	[TOKEN_LESS_EQUAL] 		= {NULL, 	binary, PREC_COMPARISON},
	[TOKEN_IDENTIFIER] 		= {variable,NULL,   PREC_NONE},
	[TOKEN_STRING] 			= {string, 	NULL,   PREC_NONE},
	[TOKEN_NUMBER] 			= {number, 	NULL,   PREC_NONE},
	[TOKEN_AND] 			= {NULL, 	and_, 	PREC_AND},
	[TOKEN_CLASS] 			= {NULL, 	NULL,   PREC_NONE},
	[TOKEN_ELSE] 			= {NULL, 	NULL,   PREC_NONE},
	[TOKEN_FALSE] 			= {literal,	NULL,   PREC_NONE},
	[TOKEN_FOR] 			= {NULL, 	NULL,   PREC_NONE},
	[TOKEN_FUN] 			= {NULL, 	NULL,   PREC_NONE},
	[TOKEN_IF] 				= {NULL, 	NULL,   PREC_NONE},
	[TOKEN_NIL] 			= {literal,	NULL,   PREC_NONE},
	[TOKEN_OR] 				= {NULL, 	or_, 	PREC_OR},
	[TOKEN_PRINT] 			= {NULL, 	NULL,   PREC_NONE},
	[TOKEN_EXIT] 			= {NULL, 	NULL,   PREC_NONE},
	[TOKEN_RETURN] 			= {NULL, 	NULL,   PREC_NONE},
	[TOKEN_SUPER] 			= {super_, 	NULL,   PREC_NONE},
	[TOKEN_THIS] 			= {this_, 	NULL,   PREC_NONE},
	[TOKEN_TRUE] 			= {literal, NULL,   PREC_NONE},
	[TOKEN_VAR] 			= {NULL, 	NULL,   PREC_NONE},
	[TOKEN_WHILE] 			= {NULL, 	NULL,   PREC_NONE},
	[TOKEN_ERROR] 			= {NULL, 	NULL,   PREC_NONE},
	[TOKEN_EOF]	 			= {NULL, 	NULL,   PREC_NONE},
};

// returns the rule of the given token
static ParseRule *getRule(TokenType type)
{
	return &rules[type];
}

// parses the current expression with correct precedence
static void parsePrecedence(Precedence precedence)
{
	advance();

	ParseFn prefixRule = getRule(parser.previous.type)->prefix;

	if (prefixRule == NULL)
	{
		error("Expect expression.");

		return;
	}

	bool canAssign = precedence <= PREC_ASSIGNMENT;
	prefixRule(canAssign);

	while (precedence <= getRule(parser.current.type)->precedence)
	{
		advance();

		ParseFn infixRule = getRule(parser.previous.type)->infix;

		infixRule(canAssign);
	}

	if (canAssign && match(TOKEN_EQUAL))
	{
		error("Invalid assignment target.");
		expression();
	}
}

// parse the current variable (identifier)
static uint8_t parseVariable(const char *errorMessage)
{
	consume(TOKEN_IDENTIFIER, errorMessage);

	declareVariable();
	// set the scope depth of the variable so that
	// it gets discarded when the scope ends
	if (current->scopeDepth > 0)
		return 0;

	return identifierConstant(&parser.previous);
}

static void markInitialized()
{
	if (current->scopeDepth == 0)
		return;
	current->locals[current->localCount - 1].depth =
		current->scopeDepth;
}

// declares a variable
static void declareVariable()
{
	if (current->scopeDepth == 0)
		return;

	Token *name = &parser.previous;

	// make sure the variable doesn't get re-declared
	for (int i = current->localCount - 1; i >= 0; i--)
	{
		Local *local = &current->locals[i];
		if (local->depth != -1 && local->depth < current->scopeDepth)
		{
			break;
		}

		if (identifiersEqual(name, &local->name))
		{
			error("Already a variable with this name in this scope.");
		}
	}
	addLocal(*name);
}

// emits the bytes for a global variable
static void defineVariable(uint8_t global)
{
	if (current->scopeDepth > 0)
	{
		markInitialized();
		return;
	}
	emitBytes(OP_DEFINE_GLOBAL, global);
}

static uint8_t argumentList()
{
	uint8_t argCount = 0;
	if (!check(TOKEN_RIGHT_PAREN))
	{
		do
		{
			expression();
			if (argCount == 255)
			{
				error("Can't have more than 255 arguments.");
			}
			argCount++;
		} while (match(TOKEN_COMMA));
	}
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
	return argCount;
}

static void and_(bool canAssign)
{
	int endJump = emitJump(OP_JUMP_IF_FALSE);

	emitByte(OP_POP);
	parsePrecedence(PREC_AND);

	patchJump(endJump);
}

static void or_(bool canAssign)
{
	int elseJump = emitJump(OP_JUMP_IF_FALSE);
	int endJump = emitJump(OP_JUMP);

	patchJump(elseJump);
	emitByte(OP_POP);

	parsePrecedence(PREC_OR);
	patchJump(endJump);
}

static void dot(bool canAssign)
{
	consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
	uint8_t name = identifierConstant(&parser.previous);

	if (canAssign && match(TOKEN_EQUAL))
	{
		expression();
		emitBytes(OP_SET_PROPERTY, name);
	}
	else if (match(TOKEN_LEFT_PAREN))
	{
		uint8_t argCount = argumentList();
		emitBytes(OP_INVOKE, name);
		emitByte(argCount);
	}
	else
	{
		emitBytes(OP_GET_PROPERTY, name);
	}
}

static void this_(bool canAssign)
{
	if (currentClass == NULL)
	{
		error("Can't use 'this' outside of a class.");
		return;
	}
	variable(false); // 'this' will be in slot 0 of the callframe
}

static void super_(bool canAssign)
{
	if (currentClass == NULL)
	{
		error("Can't use 'super' outside of a class.");
	}
	else if (!currentClass->hasSuperclass)
	{
		error("Can't use 'super' in a class with no superclass.");
	}

	consume(TOKEN_DOT, "Expect '.' after 'super'.");
	consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
	uint8_t name = identifierConstant(&parser.previous);

	namedVariable(syntheticToken("this"), false);
	// if a super.method is invoked user OP_SUPER_INVOKE instead
	// for peformance reasons
	if (match(TOKEN_LEFT_PAREN))
	{
		uint8_t argCount = argumentList();
		namedVariable(syntheticToken("super"), false);
		emitBytes(OP_SUPER_INVOKE, name);
		emitByte(argCount);
	}
	else
	{
		namedVariable(syntheticToken("super"), false);
		emitBytes(OP_GET_SUPER, name);
	}
	// emitBytes(OP_GET_SUPER, name);
}

// -------- expr/stmt stuff --------

// compile an expression
static void expression()
{
	parsePrecedence(PREC_ASSIGNMENT);
}

// compile a block
static void block()
{
	while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
	{
		declaration();
	}

	consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

// compiles a function
static void function(FunctionType type)
{
	Compiler compiler;
	initCompiler(&compiler, type);
	beginScope();

	consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");

	// parameters
	if (!check(TOKEN_RIGHT_PAREN))
	{
		do
		{
			current->function->arity++;
			if (current->function->arity > 255)
			{
				errorAtCurrent("Can't have more than 255 parameters.");
			}
			uint8_t constant = parseVariable("Expect parameter name.");
			defineVariable(constant);
		} while (match(TOKEN_COMMA));
	}
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
	
	// body
	consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
	block();

	ObjFunction *function = endCompiler();
	emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));

	// emit the upvalues as well
	for (int i = 0; i < function->upvalueCount; i++)
	{
		emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
		emitByte(compiler.upvalues[i].index);
	}
}

// compiles a function
static void method()
{
	consume(TOKEN_IDENTIFIER, "Expect method name.");
	uint8_t constant = identifierConstant(&parser.previous);

	FunctionType type = TYPE_METHOD;
	
	// check if the method is init()
	if (parser.previous.length == 4 && memcmp(parser.previous.start, "init", 4) == 0)
	{
		type = TYPE_INITIALIZER;
	}

	function(type);
	emitBytes(OP_METHOD, constant);
}

// compile a declaration
static void declaration()
{
	if (match(TOKEN_CLASS))
	{
		classDeclaration();
	}
	else if (match(TOKEN_FUN))
	{
    	funDeclaration();
  	}
	else if (match(TOKEN_VAR))
	{
		varDeclaration();
	}
	else
	{
		statement();
	}

	if (parser.panicMode)
		synchronize();
}

// compile a statement
static void statement()
{
	if (match(TOKEN_PRINT))
		printStatement();

	else if (match(TOKEN_FOR))
    	forStatement();

	else if (match(TOKEN_IF))
		ifStatement();

	else if (match(TOKEN_RETURN))
    	returnStatement();

	else if (match(TOKEN_EXIT))
		exitStatement();

	else if (match(TOKEN_WHILE))
		whileStatement();

	else if (match(TOKEN_LEFT_BRACE))
	{
		beginScope();
		block();
		endScope();
	}
	else
		expressionStatement();
}

// compiles a variable declaration
static void varDeclaration()
{
	uint8_t global = parseVariable("Expect variable name.");

	if (match(TOKEN_EQUAL))
	{
		expression();
	}
	else
	{
		emitByte(OP_NIL);
	}
	consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

	defineVariable(global);
}

// compiles a class declaration
static void classDeclaration()
{
	// name
	consume(TOKEN_IDENTIFIER, "Expect class name.");
	Token className = parser.previous; // the class Value can be anywhere
									   // on the stack so we need to remember
									   // its name
	uint8_t nameConstant = identifierConstant(&parser.previous);
	declareVariable();

	emitBytes(OP_CLASS, nameConstant);
	defineVariable(nameConstant);

	// let the compiler know we're compiling a class
	ClassCompiler classCompiler;
	classCompiler.hasSuperclass = false;
	classCompiler.enclosing = currentClass;
	currentClass = &classCompiler;

	// inheritance
	if (match(TOKEN_LESS))
	{
		consume(TOKEN_IDENTIFIER, "Expect superclass name.");
		variable(false);

		if (identifiersEqual(&className, &parser.previous))
		{
			error("A class can't inherit from itself.");
		}

		beginScope();
		addLocal(syntheticToken("super"));
		defineVariable(0);

		namedVariable(className, false);
		emitByte(OP_INHERIT);

		classCompiler.hasSuperclass = true;
	}

	namedVariable(className, false); // load the class again so that we can use it

	// methods and fields
	consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
	
	while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
	{
		method();
	}

	consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
	emitByte(OP_POP); // pop the class we loaded off the stack

	if (classCompiler.hasSuperclass)
	{
		endScope();
	}

	currentClass = currentClass->enclosing;
}

// compiles a function declaration
static void funDeclaration()
{
	uint8_t global = parseVariable("Expect function name.");
	markInitialized();
	function(TYPE_FUNCTION);
	defineVariable(global);
}

// compiles an expression statement
static void expressionStatement()
{
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
	emitByte(OP_POP);
}

// compiles a print statement
static void printStatement()
{
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after value.");
	emitByte(OP_PRINT);
}

// compiles a return statement
static void returnStatement()
{
	if (current->type == TYPE_SCRIPT)
	{
		error("Can't return from top-level code.");
	}

	if (match(TOKEN_SEMICOLON))
	{
		emitReturn();
	}
	else
	{
		if (current->type == TYPE_INITIALIZER)
		{
			error("Can't return a value from an initializer.");
		}

		expression();
		consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
		emitByte(OP_RETURN);
	}
}

// compiles an exit statement
static void exitStatement()
{
	if (match(TOKEN_SEMICOLON))
	{
		// uint32_t retval = makeConstant(NUMBER_VAL(0));
		emitConstant(NUMBER_VAL(0));
		emitByte(OP_EXIT);
	}
	else
	{
		expression();
		consume(TOKEN_SEMICOLON, "Expect ';' after exit value.");
		emitByte(OP_EXIT);
	}
}

// compiles an if statement
static void ifStatement()
{
	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

	int thenJump = emitJump(OP_JUMP_IF_FALSE);
	emitByte(OP_POP);
	statement();

	int elseJump = emitJump(OP_JUMP);

	patchJump(thenJump);
	emitByte(OP_POP);

	if (match(TOKEN_ELSE))
		statement();
	patchJump(elseJump);
}

// compiles a for statement
static void forStatement()
{
	beginScope();

	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
	if (match(TOKEN_SEMICOLON))
	{
		// No initializer.
	}
	else if (match(TOKEN_VAR))
	{
		varDeclaration();
	}
	else
	{
		expressionStatement();
	}

	int loopStart = currentChunk()->count;

	int exitJump = -1;
	if (!match(TOKEN_SEMICOLON))
	{
		expression();
		consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

		// Jump out of the loop if the condition is false.
		exitJump = emitJump(OP_JUMP_IF_FALSE);
		emitByte(OP_POP); // Condition.
	}

	if (!match(TOKEN_RIGHT_PAREN))
	{
		int bodyJump = emitJump(OP_JUMP);
		int incrementStart = currentChunk()->count;
		expression();
		emitByte(OP_POP);
		consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

		emitLoop(loopStart);
		loopStart = incrementStart;
		patchJump(bodyJump);
	}

	statement();
	emitLoop(loopStart);
	if (exitJump != -1)
	{
		patchJump(exitJump);
		emitByte(OP_POP); // Condition.
	}
	endScope();
}

// compiles a while statement
static void whileStatement()
{
	int loopStart = currentChunk()->count;

	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

	int exitJump = emitJump(OP_JUMP_IF_FALSE);
	emitByte(OP_POP);
	statement();
	emitLoop(loopStart);

	patchJump(exitJump);
	emitByte(OP_POP);
}

// compile a grouping
static void grouping(bool canAssign)
{
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

// compiles a number
static void number(bool canAssign)
{
	double value = strtod(parser.previous.start, NULL);
	emitConstant(NUMBER_VAL(value));
}

// compiles an array
static void b_brace(bool canAssing)
{
	int length = 0;
	while (!check(TOKEN_RIGHT_B_BRACE) && !check(TOKEN_EOF))
	{
		expression();
		if (!check(TOKEN_RIGHT_B_BRACE))
			consume(TOKEN_COMMA, "Expect ',' between values");
		length++;
	}
	consume(TOKEN_RIGHT_B_BRACE, "Expect ']' after array.");

	emitBytes(OP_ARRAY, (uint8_t)length);
}

// compiles a string
static void string(bool canAssign)
{
	emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

static void namedVariable(Token name, bool canAssign)
{
	uint8_t getOp, setOp;
	int arg = resolveLocal(current, &name);
	if (arg != -1)
	{
		getOp = OP_GET_LOCAL;
		setOp = OP_SET_LOCAL;
	}
	else if ((arg = resolveUpvalue(current, &name)) != -1)
	{
		getOp = OP_GET_UPVALUE;
		setOp = OP_SET_UPVALUE;
	}
	else
	{
		arg = identifierConstant(&name);
		getOp = OP_GET_GLOBAL;
		setOp = OP_SET_GLOBAL;
	}

	if (canAssign && match(TOKEN_EQUAL))
	{
		expression();
		emitBytes(setOp, (uint8_t)arg);
	}
	else
	{
		emitBytes(getOp, (uint8_t)arg);
	}
}

// compiles a variable
static void variable(bool canAssign)
{
	namedVariable(parser.previous, canAssign);
}

// compile a unary expression
static void unary(bool canAssign)
{
	TokenType operatorType = parser.previous.type;

	// Compile the operand.
	expression();

	// Emit the operator instruction.
	switch (operatorType)
	{
	case TOKEN_BANG:
		emitByte(OP_NOT);
		break;
	case TOKEN_MINUS:
		emitByte(OP_NEGATE);
		break;
	default:
		return; // Unreachable.
	}
}

// parses a binary expression
static void binary(bool canAssign)
{
	TokenType operatorType = parser.previous.type;
	ParseRule *rule = getRule(operatorType);
	parsePrecedence((Precedence)(rule->precedence + 1));

	switch (operatorType)
	{
	case TOKEN_BANG_EQUAL:
		emitBytes(OP_EQUAL, OP_NOT);
		break;
	case TOKEN_EQUAL_EQUAL:
		emitByte(OP_EQUAL);
		break;
	case TOKEN_GREATER:
		emitByte(OP_GREATER);
		break;
	case TOKEN_GREATER_EQUAL:
		emitBytes(OP_LESS, OP_NOT);
		break;
	case TOKEN_LESS:
		emitByte(OP_LESS);
		break;
	case TOKEN_LESS_EQUAL:
		emitBytes(OP_GREATER, OP_NOT);
		break;
	case TOKEN_PLUS:
		emitByte(OP_ADD);
		break;
	case TOKEN_MINUS:
		emitByte(OP_SUBTRACT);
		break;
	case TOKEN_STAR:
		emitByte(OP_MULTIPLY);
		break;
	case TOKEN_SLASH:
		emitByte(OP_DIVIDE);
		break;
	default:
		return; // Unreachable.
	}
}

// parses a call
static void call(bool canAssign)
{
	uint8_t argCount = argumentList();
	emitBytes(OP_CALL, argCount);
}

// parses a literal
static void literal(bool canAssign)
{
	switch (parser.previous.type)
	{
	case TOKEN_FALSE:
		emitByte(OP_FALSE);
		break;
	case TOKEN_NIL:
		emitByte(OP_NIL);
		break;
	case TOKEN_TRUE:
		emitByte(OP_TRUE);
		break;
	default:
		return; // Unreachable.
	}
}

// -------- control stuff --------

static void initCompiler(Compiler *compiler, FunctionType type)
{
	compiler->enclosing = current;
	compiler->function = newFunction();
	compiler->type = type;
	compiler->localCount = 0;
	compiler->scopeDepth = 0;
	current = compiler;

	if (type != TYPE_SCRIPT)
	{
		current->function->name = 
			copyString(parser.previous.start, parser.previous.length);
	}

	// the first local slot is automatically used for
	// call frame reasons	
	Local *local = &current->locals[current->localCount++];
	local->depth = 0;
	local->isCaptured = false;
	if (type != TYPE_FUNCTION)
	{
		// we're in a method so yea
		local->name.start = "this";
		local->name.length = 4;
	}
	else
	{
		local->name.start = "";
		local->name.length = 0;
	}
}	

// end the compilation process
static ObjFunction* endCompiler()
{
	emitReturn();
	ObjFunction *function = current->function;
#ifdef DEBUG_PRINT_CODE
	if (!parser.hadError)
	{
		disassembleChunk(currentChunk(),
			function->name != NULL ? function->name->chars : "<script>");
	}
#endif
	current = current->enclosing;
	return function;
}

static void beginScope()
{
	current->scopeDepth++;
}
static void endScope()
{
	current->scopeDepth--;
	// pop locals from discarded scope
	while (current->localCount > 0 &&
		   current->locals[current->localCount - 1].depth >
			   current->scopeDepth)
	{
		if (current->locals[current->localCount - 1].isCaptured)
		{
			emitByte(OP_CLOSE_UPVALUE);
		}
		else
		{
			emitByte(OP_POP);
		}
		current->localCount--;
	}
}

// main compile function
ObjFunction* compile(const char *source)
{
	initScanner(source);
	Compiler compiler;
	initCompiler(&compiler, TYPE_SCRIPT);
	parser.hadError = false;
	parser.panicMode = false;

	advance();
	while (!match(TOKEN_EOF))
	{
		declaration(); // from compile()
	}

	// consume(TOKEN_EOF, "Expect end of expression.");
	ObjFunction *function = endCompiler();
	return parser.hadError ? NULL : function;
}

// gc stuff
void markCompilerRoots()
{
	Compiler *compiler = current;
	while (compiler != NULL)
	{
		markObject((Obj *)compiler->function);
		compiler = compiler->enclosing;
	}
}
