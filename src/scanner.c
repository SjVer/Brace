#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

typedef struct
{
	const char *start;
	const char *current;
	int line;
} Scanner;

Scanner scanner;

void initScanner(const char *source)
{
	scanner.start = source;
	scanner.current = source;
	scanner.line = 1;
}

static bool isAtEnd()
{
	return *scanner.current == '\0';
}

static bool isDigit(char c)
{
	return c >= '0' && c <= '9';
}

static bool isAlpha(char c)
{
	return (c >= 'a' && c <= 'z') ||
		   (c >= 'A' && c <= 'Z') ||
		   c == '_';
}

static bool match(char expected)
{
	if (isAtEnd())
		return false;
	if (*scanner.current != expected)
		return false;
	scanner.current++;
	return true;
}

static char advance()
{
	scanner.current++;
	return scanner.current[-1];
}

static char peek()
{
	return *scanner.current;
}

static char peekNext()
{
	if (isAtEnd())
		return '\0';
	return scanner.current[1];
}

static TokenType checkKeyword(int start, int length, const char *rest, TokenType type)
{
	if (scanner.current - scanner.start == start + length &&
		memcmp(scanner.start + start, rest, length) == 0)
	{
		return type;
	}

	return TOKEN_IDENTIFIER;
}

// matches keywords
static TokenType identifierType()
{
	switch (scanner.start[0])
	{
	case 'C': return checkKeyword(1, 2, "ls", TOKEN_CLASS);
	case 'E': // 'Exit' or 'Else'
		if (scanner.current - scanner.start > 1)
		{
			switch (scanner.start[1])
			{
				case 'x': return checkKeyword(2, 2, "it", TOKEN_EXIT);
				case 'l': return checkKeyword(2, 2, "se", TOKEN_ELSE);
			}
		}
	case 'f': return checkKeyword(1, 4, "alse", TOKEN_FALSE);
	case 'F': // 'Fun' or 'For' or 'Foreach'
		if (scanner.current - scanner.start > 1)
		{
			switch (scanner.start[1])
			{
			// case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
			case 'o': // 'For' or 'Foreach'
				if (scanner.current - scanner.start > 2 && scanner.start[2] == 'r')
				{
					if (scanner.current - scanner.start > 3)
						return checkKeyword(3, 4, "each", TOKEN_FOREACH);
					else
						return checkKeyword(3, 0, "", TOKEN_FOR);
				}

			case 'u': return checkKeyword(2, 1, "n", TOKEN_FUN);
			}
		}
	case 'I': return checkKeyword(1, 1, "f", TOKEN_IF);
	case 'n': return checkKeyword(1, 3, "ull", TOKEN_NULL);
	// case 'P': return checkKeyword(1, 4, "rint", TOKEN_PRINT);
	case 'P': // 'Print' or 'PrintLn'
		if (scanner.current - scanner.start > 4)
		{
			if (scanner.current - scanner.start > 5)
				return checkKeyword(5, 2, "Ln", TOKEN_PRINT_LN);
			else
				return checkKeyword(5, 0, "", TOKEN_PRINT);
		}
	case 'R': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
	case 's': return checkKeyword(1, 4, "uper", TOKEN_SUPER);
	case 't': // 'this' or 'true'
		if (scanner.current - scanner.start > 1)
		{
			switch (scanner.start[1])
			{
			case 'h': return checkKeyword(2, 2, "is", TOKEN_THIS);
			case 'r': return checkKeyword(2, 2, "ue", TOKEN_TRUE);
			}
		}
	case 'V': return checkKeyword(1, 2, "ar", TOKEN_VAR);
	case 'W': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
	case 'U': return checkKeyword(1, 2, "se", TOKEN_USE);
	}
	return TOKEN_IDENTIFIER;
}

static Token makeToken(TokenType type)
{
	Token token;
	token.type = type;
	token.start = scanner.start;
	token.length = (int)(scanner.current - scanner.start);
	token.line = scanner.line;
	return token;
}

static Token errorToken(const char *message)
{
	Token token;
	token.type = TOKEN_ERROR;
	token.start = message;
	token.length = (int)strlen(message);
	token.line = scanner.line;
	return token;
}

static Token string()
{
	while (peek() != '"' && !isAtEnd())
	{
		if (peek() == '\n')
			scanner.line++;
		else if (peek() == '\\')
			advance();
		advance();
	}

	if (isAtEnd())
		return errorToken("Unterminated string.");

	// The closing quote.
	advance();
	return makeToken(TOKEN_STRING);
}

static Token number()
{
	while (isDigit(peek()))
		advance();

	// Look for a fractional part.
	if (peek() == '.' && isDigit(peekNext()))
	{
		// Consume the ".".
		advance();

		while (isDigit(peek()))
			advance();
	}

	return makeToken(TOKEN_NUMBER);
}

static Token identifier()
{
	while (isAlpha(peek()) || isDigit(peek()))
		advance();
	return makeToken(identifierType());
}

static void skipWhitespace()
{
	for (;;)
	{
		char c = peek();
		switch (c)
		{
		case ' ':
		case '\r':
		case '\t':
			advance();
			break;
		case '\n':
			scanner.line++;
			advance();
			break;
		case '#':
			if (peekNext() == '*')
			{
				// while (peek() != '*' && peekNext() != '#' && !isAtEnd())
				for (;;)
				{
					if (peek() == '*' && peekNext() == '#')
					{
						advance();
						advance();
						break;
					}
					else if (isAtEnd()) break;
					else if (peek() == '\n') scanner.line++;
					advance();
				}
			}
			else
			{
				while (peek() != '\n' && !isAtEnd())
					advance();
			}
			break;
		// case '/':
		// 	if (peekNext() == '/')
		// 	{
		// 		// A comment goes until the end of the line.
		// 		while (peek() != '\n' && !isAtEnd())
		// 			advance();
		// 	}
		// 	else
		// 	{
		// 		return;
		// 	}
		// 	break;
		default:
			return;
		}
	}
}

Token scanToken()
{
	skipWhitespace();

	scanner.start = scanner.current;

	if (isAtEnd())
		return makeToken(TOKEN_EOF);

	char c = advance();

	// check for idents
	if (isAlpha(c))
		return identifier();
	// check for digits
	if (isDigit(c))
		return number();

	switch (c)
	{
	// single-character
	// case '\n': return makeToken(TOKEN_NEWLINE);

	case '(': return makeToken(TOKEN_LEFT_PAREN);
	case ')': return makeToken(TOKEN_RIGHT_PAREN);
	case '{': return makeToken(TOKEN_LEFT_BRACE);
	case '}': return makeToken(TOKEN_RIGHT_BRACE);
	case '[': return makeToken(TOKEN_LEFT_B_BRACE);
	case ']': return makeToken(TOKEN_RIGHT_B_BRACE);
	case '?': return makeToken(TOKEN_QUESTION);
	case ':': return makeToken(TOKEN_COLON);
	case ';': return makeToken(TOKEN_SEMICOLON);
	case ',': return makeToken(TOKEN_COMMA);
	case '.': return makeToken(TOKEN_DOT);
	case '/': return makeToken(TOKEN_SLASH);
	case '*': return makeToken(TOKEN_STAR);
	case '%': return makeToken(TOKEN_MODULO);

	// two-character
	// case '+': return makeToken(match('+') ? TOKEN_PLUS_PLUS : TOKEN_PLUS);
	case '+': return makeToken(match('+') ? TOKEN_PLUS_PLUS     : match('=') ? TOKEN_PLUS_EQUAL  : TOKEN_PLUS);
	case '-': return makeToken(match('-') ? TOKEN_MINUS_MINUS   : match('=') ? TOKEN_MINUS_EQUAL : match('>') ? TOKEN_ARROW : TOKEN_MINUS);
	case '!': return makeToken(match('=') ? TOKEN_BANG_EQUAL    : TOKEN_BANG);
	case '=': return makeToken(match('=') ? TOKEN_EQUAL_EQUAL   : TOKEN_EQUAL);
	case '<': return makeToken(match('=') ? TOKEN_LESS_EQUAL    : TOKEN_LESS);
	case '>': return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
	
	case '|': return match('|') ? makeToken(TOKEN_OR) : errorToken("Expected '|' after '|'.");
	case '&': return match('&') ? makeToken(TOKEN_AND) : errorToken("Expected '&' after '&'.");

	// literals
	case '"': return string();
	}

	return errorToken("Unexpected character.");
}
