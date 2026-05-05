#ifndef LEXER_H
// "If LEXER_H is not defined yet..."
#define LEXER_H
// "...define it now."

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

typedef enum
{
    INT,
    KEYWORD,
    SEPARATOR,
    IDENTIFIER,
    OPERATOR,
    STRING_LITERAL
} TokenType;

typedef struct
{
    TokenType type;
    union
    {
        int int_val;
        char *str_val;
    } value;
    // For STRING_LITERAL only: length of the decoded byte sequence.
    // (The bytes live in str_val; may contain embedded nulls, so strlen
    // is unsafe.)
    size_t str_len;
    int line;
    int col;
} Token;

Token *lexer(FILE *file, size_t *num_tokens_out);
void print_token(Token token);
void free_tokens(Token *tokens, size_t num_tokens);

#endif // LEXER_H