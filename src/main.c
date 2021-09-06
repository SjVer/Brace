#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>

#include "common.h"
#include "natives.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"
#include "mem.h"



// static struct termios old, new;
// /* Read 1 character - echo defines echo mode */
// char getch_(int echo)
// {
// 	char ch;
// 	tcgetattr(0, &old);					/* grab old terminal i/o settings */
// 	new = old;							/* make new settings same as old settings */
// 	new.c_lflag &= ~ICANON;				/* disable buffered i/o */
// 	new.c_lflag &= echo ? ECHO : ~ECHO; /* set echo mode */
// 	tcsetattr(0, TCSANOW, &new);
// 	ch = getchar();
// 	tcsetattr(0, TCSANOW, &old);
// 	return ch;
// }

// run the REPL
static void repl()
{
	printf("%s", formatString(WELCOME_MESSAGE, 
		BRACE_VERSION_0, BRACE_VERSION_1, BRACE_VERSION_2,
		__DATE__, __TIME__, COMPILER, __VERSION__, OS));

	defineNativeFn("Help", helpNative, 0);

	// bool overwrote = false;
	// char lines[100][1024];
	// int cnt = 0; // number of current line to be shown
	// for (;;)
	// {
	// 	printf("> ");
	// 	char ch; // one character input buffer
	// 	do
	// 	{
	// 		// read input (arrows. characters, etc.
	// 		ch = getch_(0);
	// 		// arrows detection in input
	// 		if (ch == 27)
	// 		{
	// 			ch = getch_(0);
	// 			if (ch == 91)
	// 			{
	// 				ch = getch_(0);
	// 				if (ch == 66) // up arrow
	// 				{
	// 					// printf("\r");
	// 					cnt++;
	// 					if (cnt >= 100)
	// 					{
	// 						cnt = 0;
	// 						overwrote = true;
	// 					}
	// 					for (int i; i < strlen(lines[cnt]); i++)
	// 						printf("_");
	// 					printf("\r> %s", lines[cnt]);
	// 				}
	// 				else if (ch == 65) // down arrow
	// 				{
	// 					// printf("\r");
	// 					cnt--;
	// 					if (cnt < 0)
	// 						cnt = overwrote ? 100 - 1 : 0;
	//
	// 					for (int i; i < strlen(lines[cnt]); i++)
	// 						printf("_");
	// 					printf("\r> %s", lines[cnt]);
	// 				}
	// 			}
	// 		}
	// 		else if (ch == '\n' || ch == EOF)
	// 			break;
	// 		else
	// 		{
	// 			// append char
	// 			int len = strlen(lines[cnt]);
	// 			lines[cnt][len] = ch;
	// 			lines[cnt][len + 1] = '\0';
	// 			printf("%c", ch);
	// 		}
	// 	} while (1);
	// 	printf("\n");
	// 	interpret(lines[cnt]);
	// 	cnt++;
	// }
	
	char *prevline = "";
	char line[1024];
	int depth = 0;

	for (;;)
	{
		#ifdef DEBUG_TRACE_EXECUTION
			printf("\n");
		#endif

		// prompt
		printf(depth == 0 ? PROMPT_NORM : PROMPT_IND);
	
		// get input
		if (!fgets(line, sizeof(line), stdin))
		{
			printf("\n");
			break;
		}

		// trim trailing whitespaces
		{
			int index, i;

			/* Set default index */
			index = -1;

			/* Find last index of non-white space character */
			i = 0;
			while (line[i] != '\0')
			{
				if (line[i] != ' ' && line[i] != '\t' && line[i] != '\n')
				{
					index = i;
				}

				i++;
			}

			/* Mark next character to last non-white space character as NULL */
			line[index + 1] = '\0';
		}

		// handle blocks
		for (int i = 0; i < strlen(line); i++)
		{
			if (line[i] == '{')
				depth++;
			else if (line[i] == '}')
				depth = depth-- >= 0 ? depth : 0;
		}

		// interpret
		if (depth == 0)
		{
			interpret("<repl>", formatString("%s\n%s", prevline, line), true);
			prevline = "";
		}
		else prevline = formatString("%s\n%s", prevline, line);
	}
}

// read the given file to a string
static char *readFile(const char *path)
{
	FILE *file = fopen(path, "rb");
	if (file == NULL)
	{
		fprintf(stderr, "Could not open file \"%s\".\n", path);
		exit(74);
	}

	fseek(file, 0L, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);

	char *buffer = (char *)malloc(fileSize + 1);
	if (buffer == NULL)
	{
		fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
		exit(74);
	}
	size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
	if (bytesRead < fileSize)
	{
		fprintf(stderr, "Could not read file \"%s\".\n", path);
		exit(74);
	}
	buffer[bytesRead] = '\0';

	fclose(file);
	return buffer;
}

// run the given file
static void runFile(const char *path)
{
	char *source = readFile(path);
	InterpretResult result = interpret(path, source, false);
	free(source);

	if (result == INTERPRET_COMPILE_ERROR)
		exit(65);
	if (result == INTERPRET_RUNTIME_ERROR)
		exit(70);
}

int main(int argc, const char *argv[])
{
	// bool debug = false;

	initVM(false);

	// handle command line args
	if (argc == 1)
	{
		repl();
	}
	else if (argc == 2)
	{
		runFile(argv[1]);
	}
	else
	{
		fprintf(stderr, "Usage: brace [path]\n");
		exit(64);
	}

	freeVM();
	return 0;
}