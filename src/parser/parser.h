#ifndef PARSER_H
// "If PARSER_H is not defined yet..."
#define PARSER_H
// "...define it now."

#include "../lexer/lexer.h"
#include "../output/output_log.h"

typedef enum
{
    NODE_LITERAL_INT,
    NODE_BINARY_EXPR,
    NODE_EXIT_CALL,
    NODE_IF_STATEMENT,
    NODE_ELSE_IF_STATEMENT,
    NODE_ELSE_STATEMENT,
    NODE_WHILE_STATEMENT,
    NODE_DO_WHILE_STATEMENT,
    NODE_FOR_STATEMENT,
    NODE_PRINTF_CALL,
    NODE_BREAK,
    NODE_CONTINUE,
    NODE_IDENTIFIER,
    NODE_STATEMENT_END,
    NODE_UNKNOWN,
    NODE_BEGIN,

    // NODE_CONST_DECL,
    NODE_VAR_DECL,

    NODE_TYPE_SPECIFIER,
    NODE_ASSIGNMENT,
    NODE_BLOCK
} NodeType;

typedef struct Node
{
    NodeType type;
    union
    {
        int int_val;
        char *str_val;
    } value;
    int line;
    int col;
    struct Node *left;  // First child
    struct Node *right; // Next sibling
    // Node *parent; // Optional for upstream traversal
} Node;

// Symbol Table Implementation
typedef enum
{
    VAR_INT,
    // VAR_CONST_INT
} VarType;

typedef struct Symbol
{
    char *name;
    VarType type;
    int value;
    int line;
    int col;
    int is_const;  // 1 = immutable after init; assignment is an error
} Symbol;

typedef struct SymbolTable
{
    Symbol *symbols;
    size_t size;
    size_t capacity;
} SymbolTable;

typedef struct ScopeStack
{
    SymbolTable **tables;
    size_t size;
    size_t capacity;
} ScopeStack;

// Propagated through block parsing inside loops so break/continue
// can short-circuit the simulation.
typedef struct
{
    int breaking;     // set by `break;`
    int continuing;   // set by `continue;`
} LoopControl;

Node *parse(Token *tokens, size_t num_tokens, OutputLog *log);
void free_ast(Node *node);

// left child right sibling
void treeTraversal(Node *node, int depth);

#endif // PARSER_H