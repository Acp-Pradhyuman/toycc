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
    NODE_SWITCH_STATEMENT,
    NODE_CASE,
    NODE_DEFAULT,
    NODE_RETURN,
    NODE_FUNCTION_DEF,
    NODE_FUNCTION_CALL,
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

// Propagated through block parsing inside a switch. `break` binds to the
// innermost switch (not the enclosing loop). `continue` inside a switch
// still binds to the outer LoopControl (standard C semantics).
typedef struct
{
    int breaking;
} SwitchControl;

// Set on a call frame when `return;` is executed. Simulator short-circuits
// all remaining work inside the function body.
typedef struct
{
    int returning;
    int value;
    int is_void;       // 1 if the enclosing function is declared void
    const char *fname; // for better error messages
} ReturnState;

// Aggregated control-flow state handed to block/statement parsers so we
// can add new control constructs without changing every signature.
// Any field may be NULL to signal "no enclosing construct of that kind".
typedef struct
{
    LoopControl *lc;      // innermost enclosing loop, or NULL
    SwitchControl *sc;    // innermost enclosing switch, or NULL
    ReturnState *rs;      // current function's return slot, or NULL
} FlowCtx;

typedef enum
{
    RET_INT,    // int or char (both stored as int in toycc)
    RET_VOID,
} ReturnType;

// Function symbol table. Stores name, parameter names, and token range
// of the body so call sites can re-parse it with fresh scopes.
typedef struct
{
    char *name;
    ReturnType return_type;
    char **param_names;
    size_t num_params;
    size_t body_start;   // token index of the '{' that opens the body
    size_t body_end;     // token index of the matching '}'
    int def_line;
    int def_col;
} FunctionSymbol;

typedef struct
{
    FunctionSymbol *funcs;
    size_t size;
    size_t capacity;
} FunctionTable;

Node *parse(Token *tokens, size_t num_tokens, OutputLog *log);
void free_ast(Node *node);

// left child right sibling
void treeTraversal(Node *node, int depth);

#endif // PARSER_H