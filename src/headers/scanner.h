#ifndef brace_scanner_h
#define brace_scanner_h

typedef enum
{
	// Single-character tokens.
	TOKEN_LEFT_PAREN,
	TOKEN_RIGHT_PAREN,
	TOKEN_LEFT_BRACE,
	TOKEN_RIGHT_BRACE,
	TOKEN_LEFT_B_BRACE,
	TOKEN_RIGHT_B_BRACE,
	TOKEN_COMMA,
	TOKEN_DOT,
	TOKEN_MINUS,
	TOKEN_MINUS_MINUS,
	TOKEN_PLUS,
	TOKEN_PLUS_PLUS,
	TOKEN_QUESTION,
	TOKEN_COLON,
	TOKEN_SEMICOLON,
	TOKEN_NEWLINE,
	TOKEN_SLASH,
	TOKEN_STAR,
	TOKEN_MODULO,

	// One or two character tokens.
	TOKEN_BANG,
	TOKEN_BANG_EQUAL,
	TOKEN_EQUAL,
	TOKEN_EQUAL_EQUAL,
	TOKEN_GREATER,
	TOKEN_LESS,
	TOKEN_GREATER_EQUAL,
	TOKEN_LESS_EQUAL,
	TOKEN_ARROW,
	TOKEN_MINUS_EQUAL,
	TOKEN_PLUS_EQUAL,

	// Literals.
	TOKEN_IDENTIFIER,
	TOKEN_STRING,
	TOKEN_NUMBER,

	// Keywords.
	TOKEN_AND,
	TOKEN_CLASS,
	TOKEN_USE,
	TOKEN_ELSE,
	TOKEN_FALSE,
	TOKEN_FOR,
	TOKEN_FOREACH,
	TOKEN_FUN,
	TOKEN_IF,
	TOKEN_NULL,
	TOKEN_OR,
	TOKEN_PRINT,
	TOKEN_PRINT_LN,
	TOKEN_EXIT,
	TOKEN_RETURN,
	TOKEN_SUPER,
	TOKEN_THIS,
	TOKEN_TRUE,
	TOKEN_VAR,
	TOKEN_WHILE,

	// misc.
	TOKEN_ERROR,
	TOKEN_EOF
} TokenType;

typedef struct
{
	TokenType type;
	const char *start;
	int length;
	int line;
} Token;

// initialize the scanner
void initScanner(const char *source);
// scan the next token
Token scanToken();

#endif
