#include "lexer.h"

#define INITIAL_TOKEN_CAPACITY 64

// should be local to lexer only hence static
static int col = 1, line = 1;

bool is_binary_digit(char ch)
{
    return ch == '0' || ch == '1';
}

int bin_to_digit(char ch)
{
    return ch - '0';
}

bool is_octal_digit(char ch)
{
    return ch >= '0' && ch <= '7';
}

int oct_to_digit(char ch)
{
    return ch - '0';
}

bool is_decimal_digit(char ch)
{
    return isdigit((unsigned char)ch);
}

int dec_to_digit(char ch)
{
    return ch - '0';
}

bool is_hex_digit(char ch)
{
    return isdigit((unsigned char)ch) ||
           (ch >= 'a' && ch <= 'f') ||
           (ch >= 'A' && ch <= 'F');
}

int hex_to_digit(char ch)
{
    if (isdigit((unsigned char)ch))
        return ch - '0';
    else if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    else // (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
}

typedef bool (*IsValidDigitFunc)(char);
typedef int (*CharToDigitFunc)(char);

int parse_number_in_base(FILE *file, int *col, int base,
                         IsValidDigitFunc is_valid, CharToDigitFunc to_digit,
                         char initial_digit)
{
    int number = to_digit(initial_digit);
    char ch;

    while ((ch = fgetc(file)) != EOF)
    {
        if (is_valid(ch))
        {
            number = number * base + to_digit(ch);
            (*col)++;
        }
        else
        {
            ungetc(ch, file);
            (*col)--;
            break;
        }
    }

    return number;
}

void print_token(Token token)
{
    printf("[Line %d, Col %d] ", token.line, token.col);
    switch (token.type)
    {
    case INT:
        printf("Token(INT): %d\n", token.value.int_val);
        break;
    case KEYWORD:
        printf("Token(KEYWORD): %s\n", token.value.str_val);
        break;
    case SEPARATOR:
        printf("Token(SEPARATOR): %s\n", token.value.str_val);
        break;
    case IDENTIFIER:
        printf("Token(IDENTIFIER): %s\n", token.value.str_val);
        break;
    case OPERATOR:
        printf("Token(OPERATOR): %s\n", token.value.str_val);
        break;
    default:
        printf("Unknown token type\n");
        break;
    }
}

int generate_number(char ch, FILE *file, int *col)
{
    int number = 0;

    if (ch == '0')
    {
        int next = fgetc(file);
        (*col)++;

        if (next == 'x' || next == 'X')
        {
            char first = fgetc(file);
            (*col)++;
            if (!is_hex_digit(first))
            {
                fprintf(stderr, "Invalid hex literal at line %d, \
                    col %d\n",
                        line, *col);
                exit(EXIT_FAILURE);
            }
            number = parse_number_in_base(file, col, 16,
                                          is_hex_digit, hex_to_digit, first);
        }
        else if (next == 'b' || next == 'B')
        {
            char first = fgetc(file);
            (*col)++;
            if (!is_binary_digit(first))
            {
                fprintf(stderr, "Invalid binary literal at line %d, \
                    col %d\n",
                        line, *col);
                exit(EXIT_FAILURE);
            }
            number = parse_number_in_base(file, col, 2,
                                          is_binary_digit, bin_to_digit, first);
        }
        else if (is_octal_digit(next))
        {
            number = parse_number_in_base(file, col, 8,
                                          is_octal_digit, oct_to_digit, next);
        }
        else
        {
            ungetc(next, file);
            (*col)--;
            number = 0;
        }
    }
    // else if (isdigit(ch))
    // {
    //     number = parse_number_in_base(file, col, 10,
    //                                   is_decimal_digit, dec_to_digit);
    // }
    // else
    // {
    //     number = 0;
    // }
    else
    {
        number = parse_number_in_base(file, col, 10,
                                      is_decimal_digit, dec_to_digit, ch);
    }

    return number;
}

int read_identifier(char first_char, FILE *file, int *col, char *buffer,
                    size_t buf_size)
{
    int length = 0;
    char ch;

    buffer[length++] = first_char;

    while ((ch = fgetc(file)) != EOF && isalnum(ch))
    {
        if (length < (int)(buf_size - 1))
        {
            buffer[length++] = ch;
        }
        (*col)++;
    }

    buffer[length] = '\0';

    if (ch != EOF)
    {
        ungetc(ch, file);
        (*col)--;
    }

    return length;
}

void free_tokens(Token *tokens, size_t num_tokens)
{
    if (!tokens)
        return;

    for (size_t i = 0; i < num_tokens; ++i)
    {
        // Free string values if token type uses str_val
        if (tokens[i].type == KEYWORD ||
            tokens[i].type == SEPARATOR ||
            tokens[i].type == IDENTIFIER ||
            tokens[i].type == OPERATOR)
        {
            if (tokens[i].value.str_val)
            {
                free(tokens[i].value.str_val);
                tokens[i].value.str_val = NULL;
            }
        }
    }

    free(tokens);
}

Token *lexer(FILE *file, size_t *num_tokens_out)
{
    size_t capacity = INITIAL_TOKEN_CAPACITY;
    size_t count = 0;
    Token *tokens = malloc(capacity * sizeof(Token));
    if (!tokens)
    {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }

    char ch = fgetc(file);
    while (ch != EOF)
    {
        if (count >= capacity)
        {
            capacity *= 2;
            tokens = realloc(tokens, capacity * sizeof(Token));
            if (!tokens)
            {
                fprintf(stderr, "Reallocation failed\n");
                exit(EXIT_FAILURE);
            }
        }

        if (ch == '\n')
        {
            // Token token;
            // token.type = SEPARATOR;
            // token.value.str_val = strdup("new line");
            // token.col = col;
            // token.line = line;
            printf("Found new line at line %d and col %d\n", line, col);
            // print_token(token);
            // tokens[count++] = token;
            line++;
            col = 0;
            // col = 1;
            // ch = fgetc(file);
            // continue;
        }

        else if (isspace(ch))
        {
            // Token token;
            // token.type = SEPARATOR;
            // char temp[2] = {ch, '\0'};
            // token.value.str_val = strdup(temp);
            // token.col = col;
            // token.line = line;
            printf("Found whitespace at line %d and col %d\n", line, col);
            // print_token(token);
            // tokens[count++] = token;
        }

        else if (ch == ';')
        {
            Token token;
            token.type = SEPARATOR;
            token.value.str_val = strdup(";");
            token.col = col;
            token.line = line;
            printf("Found Semicolon at line %d and col %d\n",
                   line, col);
            print_token(token);
            tokens[count++] = token;
        }

        else if (ch == ',')
        {
            Token token;
            token.type = SEPARATOR;
            token.value.str_val = strdup(",");
            token.col = col;
            token.line = line;
            printf("Found COMMA at line %d and col %d\n", line, col);
            print_token(token);
            tokens[count++] = token;
        }

        else if (ch == '(')
        {
            Token token;
            token.type = SEPARATOR;
            token.value.str_val = strdup("(");
            token.col = col;
            token.line = line;
            printf("Found OPEN_PARENTHESIS %c at line %d and col %d\n",
                   ch, line, col);
            print_token(token);
            tokens[count++] = token;
        }

        else if (ch == '{')
        {
            Token token;
            token.type = SEPARATOR;
            token.value.str_val = strdup("{");
            token.col = col;
            token.line = line;
            printf("Found OPEN_PARENTHESIS %c at line %d and col %d\n",
                   ch, line, col);
            print_token(token);
            tokens[count++] = token;
        }

        else if (ch == ')')
        {
            Token token;
            token.type = SEPARATOR;
            token.value.str_val = strdup(")");
            token.col = col;
            token.line = line;
            printf("Found CLOSED_PARENTHESIS %c at line %d and col %d\n",
                   ch, line, col);
            print_token(token);
            tokens[count++] = token;
        }

        else if (ch == '}')
        {
            Token token;
            token.type = SEPARATOR;
            token.value.str_val = strdup("}");
            token.col = col;
            token.line = line;
            printf("Found CLOSED_PARENTHESIS %c at line %d and col %d\n",
                   ch, line, col);
            print_token(token);
            tokens[count++] = token;
        }

        else if (strchr("+-*/%=&|^!<>", ch))
        {
            int start_col = col;
            char op[4] = {ch, '\0', '\0', '\0'}; // Max 3-char operators
            char next = fgetc(file);
            col++;

            // Check for 2-char or 3-char operators
            if ((ch == '+' && next == '=') ||
                (ch == '+' && next == '+') ||
                (ch == '-' && next == '=') ||
                (ch == '-' && next == '-') ||
                (ch == '*' && next == '=') ||
                (ch == '/' && next == '=') ||
                (ch == '%' && next == '=') ||
                (ch == '=' && next == '=') ||
                (ch == '!' && next == '=') ||
                (ch == '<' && next == '=') ||
                (ch == '>' && next == '=') ||
                (ch == '&' && next == '&') ||
                (ch == '|' && next == '|') ||
                (ch == '<' && next == '<') ||
                (ch == '>' && next == '>'))
            {
                op[1] = next;

                // Check for <<= or >>=
                if ((op[0] == '<' && op[1] == '<') ||
                    (op[0] == '>' && op[1] == '>'))
                {
                    char third = fgetc(file);
                    col++;
                    if (third == '=')
                    {
                        op[2] = '=';
                    }
                    else
                    {
                        ungetc(third, file);
                        col--;
                    }
                }
            }
            else
            {
                ungetc(next, file);
                col--;
            }

            Token token;
            token.type = OPERATOR;
            token.value.str_val = strdup(op);
            token.col = start_col;
            token.line = line;
            print_token(token);
            tokens[count++] = token;
        }

        else if (isdigit(ch))
        {
            int start_col = col;
            col++;
            Token token;
            token.type = INT;
            token.value.int_val = generate_number(ch, file, &col);
            token.col = start_col;
            token.line = line;
            printf("Found number = %d at line %d and col %d\n",
                   token.value.int_val, line, start_col);
            print_token(token);
            tokens[count++] = token;
        }

        else if (isalpha(ch))
        {
            char buffer[32];
            int start_col = col;
            col++;
            int length = read_identifier(ch, file, &col, buffer, sizeof(buffer));

            Token token;
            if ((strcmp(buffer, "exit") == 0) ||
                strcmp(buffer, "int") == 0 ||
                strcmp(buffer, "if") == 0)
            {
                token.type = KEYWORD;
                token.value.str_val = strdup(buffer);
                token.col = start_col;
                token.line = line;
                printf("Found keyword '%s' at line %d and col %d\n",
                       token.value.str_val, line, start_col);
                print_token(token);
            }
            else
            {
                for (int i = 0; i < length; i++)
                {
                    printf("Found character = %c at line %d and col %d\n",
                           buffer[i], line, start_col + i);
                }
                token.type = IDENTIFIER;
                token.value.str_val = strdup(buffer);
                token.col = start_col;
                token.line = line;
                print_token(token);
            }
            tokens[count++] = token;
        }

        else
        {
            if (strchr("$#@~`", ch))
            {
                printf("ERROR: stray special character '%c' at "
                       "line %d, col %d\n",
                       ch, line, col);
            }
            else
            {
                printf("ERROR: unrecognized token '%c' (ASCII %d) at "
                       "line %d, col %d\n",
                       ch, (int)ch, line, col);
            }
            exit(EXIT_FAILURE);
        }

        ch = fgetc(file);
        col++;
    }

    *num_tokens_out = count;
    return tokens;
}