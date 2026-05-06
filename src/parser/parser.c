#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "parser.h"
#include "../error/error.h"

// Hard cap on loop-unroll iterations. Since loops are simulated at
// compile time, a runtime-style infinite loop would hang the compiler.
// The cap is large enough to not regress real tests (test14 runs ~840K
// iterations) but small enough to fail fast on genuine infinite loops.
#define MAX_LOOP_ITERATIONS 10000000

// Forward declarations
static Node *parse_expression(Token *tokens, size_t *i, size_t num_tokens,
                              ScopeStack *scope_stack, int min_precedence);
static Node *parse_primary(Token *tokens, size_t *i, size_t num_tokens,
                           ScopeStack *scope_stack);
static Node *parse_block(Token *tokens, size_t *i, size_t num_tokens,
                         ScopeStack *scope_stack, OutputLog *log,
                         LoopControl *lc,
                         bool condition_active);

void debugPrintNode(const char *prefix, Node *node)
{
    if (!node)
    {
        printf("%s: NULL\n", prefix);
        return;
    }

    printf("%s: Node type %d", prefix, node->type);
    switch (node->type)
    {
    case NODE_LITERAL_INT:
        printf(", int_val=%d\n", node->value.int_val);
        break;
    case NODE_BINARY_EXPR:
    case NODE_IDENTIFIER:
    case NODE_VAR_DECL:
    case NODE_ASSIGNMENT:
    case NODE_TYPE_SPECIFIER:
        printf(", str_val=%s\n",
               node->value.str_val ? node->value.str_val : "(null)");
        break;
    case NODE_BLOCK:
        printf(", BLOCK node\n");
        break;
    default:
        printf("\n");
        break;
    }
}

static SymbolTable *create_symbol_table()
{
    SymbolTable *table = malloc(sizeof(SymbolTable));
    if (!table)
        return NULL;
    table->symbols = malloc(sizeof(Symbol) * 16);
    if (!table->symbols)
    {
        free(table);
        return NULL;
    }
    table->size = 0;
    table->capacity = 16;
    return table;
}

static void free_symbol_table(SymbolTable *table)
{
    if (!table)
        return;
    for (size_t i = 0; i < table->size; i++)
    {
        free(table->symbols[i].name);
    }
    free(table->symbols);
    free(table);
}

static void add_symbol(SymbolTable *table, const char *name, VarType type,
                       int value, int line, int col)
{
    if (!table || !name)
        return;

    if (table->size >= table->capacity)
    {
        table->capacity *= 2;
        Symbol *new_symbols =
            realloc(table->symbols, sizeof(Symbol) * table->capacity);
        if (!new_symbols)
            return;
        table->symbols = new_symbols;
    }

    table->symbols[table->size].name = strdup(name);
    if (!table->symbols[table->size].name)
        return;
    table->symbols[table->size].type = type;
    table->symbols[table->size].value = value;
    table->symbols[table->size].line = line;
    table->symbols[table->size].col = col;
    table->symbols[table->size].is_const = 0;
    table->size++;
}

static void update_symbol(SymbolTable *table, const char *name, int value)
{
    if (!table || !name)
        return;
    for (size_t i = 0; i < table->size; i++)
    {
        if (strcmp(table->symbols[i].name, name) == 0)
        {
            table->symbols[i].value = value;
            return;
        }
    }
    printf("Error: Variable '%s' not found for update\n", name);
    exit(1);
}

static Symbol *find_symbol(SymbolTable *table, const char *name)
{
    if (!table || !name)
        return NULL;
    for (size_t i = 0; i < table->size; i++)
    {
        if (strcmp(table->symbols[i].name, name) == 0)
        {
            return &table->symbols[i];
        }
    }
    return NULL;
}

static ScopeStack *create_scope_stack()
{
    ScopeStack *stack = malloc(sizeof(ScopeStack));
    if (!stack)
        return NULL;
    stack->tables = malloc(sizeof(SymbolTable *) * 8);
    if (!stack->tables)
    {
        free(stack);
        return NULL;
    }
    stack->size = 0;
    stack->capacity = 8;
    return stack;
}

static void push_scope(ScopeStack *stack, SymbolTable *table)
{
    if (!stack || !table)
        return;
    if (stack->size >= stack->capacity)
    {
        stack->capacity *= 2;
        SymbolTable **new_tables = realloc(stack->tables,
                                           sizeof(SymbolTable *) * stack->capacity);
        if (!new_tables)
            return;
        stack->tables = new_tables;
    }
    stack->tables[stack->size++] = table;
}

static SymbolTable *pop_scope(ScopeStack *stack)
{
    if (!stack || stack->size == 0)
    {
        printf("Error: Attempt to pop empty scope stack\n");
        exit(1);
    }
    return stack->tables[--stack->size];
}

static SymbolTable *current_scope(ScopeStack *stack)
{
    if (!stack || stack->size == 0)
        return NULL;
    return stack->tables[stack->size - 1];
}

static void free_scope_stack(ScopeStack *stack)
{
    if (!stack)
        return;
    for (size_t i = 0; i < stack->size; i++)
    {
        free_symbol_table(stack->tables[i]);
    }
    free(stack->tables);
    free(stack);
}

static Symbol *find_symbol_in_scope_stack(ScopeStack *stack, const char *name)
{
    if (!stack || !name)
        return NULL;
    // Search from innermost to outermost scope
    for (int i = stack->size - 1; i >= 0; i--)
    {
        Symbol *sym = find_symbol(stack->tables[i], name);
        if (sym)
            return sym;
    }
    return NULL;
}

// Node Creation
static Node *createNode(NodeType type, const char *value, int line, int col)
{
    Node *node = malloc(sizeof(Node));
    if (!node)
        return NULL;

    node->type = type;
    node->line = line;
    node->col = col;
    node->left = NULL;
    node->right = NULL;

    if (type == NODE_LITERAL_INT)
    {
        node->value.int_val = value ? atoi(value) : 0;
    }
    else
    {
        // Only strdup if value is not NULL and we need a copy
        node->value.str_val = value ? strdup(value) : NULL;
        if (value && !node->value.str_val)
        {
            // strdup failed
            free(node);
            return NULL;
        }
    }

    return node;
}

static Node *createNodeFromToken(Token token)
{
    NodeType type;
    char *value_str = NULL;

    switch (token.type)
    {
    case INT:
        type = NODE_LITERAL_INT;
        value_str = NULL; // We'll use int_val directly
        break;
    case IDENTIFIER:
        type = NODE_IDENTIFIER;
        value_str = token.value.str_val;
        break;
    case KEYWORD:
        if (strcmp(token.value.str_val, "exit") == 0)
        {
            type = NODE_EXIT_CALL;
        }
        else if (strcmp(token.value.str_val, "if") == 0)
        {
            type = NODE_IF_STATEMENT;
        }
        else if (strcmp(token.value.str_val, "int") == 0 ||
                 strcmp(token.value.str_val, "char") == 0)
        {
            type = NODE_TYPE_SPECIFIER;
        }
        else
        {
            type = NODE_UNKNOWN;
        }
        value_str = token.value.str_val;
        break;
    case OPERATOR:
        if (strcmp(token.value.str_val, "=") == 0)
        {
            type = NODE_ASSIGNMENT;
        }
        else
        {
            type = NODE_BINARY_EXPR;
        }
        value_str = token.value.str_val;
        break;
    case SEPARATOR:
        if (strcmp(token.value.str_val, ";") == 0)
        {
            type = NODE_STATEMENT_END;
        }
        else
        {
            type = NODE_UNKNOWN;
        }
        value_str = token.value.str_val;
        break;
    default:
        type = NODE_UNKNOWN;
        value_str = NULL;
        break;
    }

    Node *node = createNode(type, value_str, token.line, token.col);
    if (!node)
        return NULL;

    if (type == NODE_LITERAL_INT)
    {
        node->value.int_val = token.value.int_val;
    }
    return node;
}

// Expression Evaluation
static int evaluate_constant_expression(Node *node)
{
    if (!node)
        return 0;

    if (node->type == NODE_LITERAL_INT)
    {
        return node->value.int_val;
    }

    if (node->type == NODE_IDENTIFIER)
    {
        // Don't fold identifiers to constants
        fprintf(stderr, "Warning: Cannot evaluate identifier '%s' as constant\n",
                node->value.str_val ? node->value.str_val : "(null)");
        // Signal non-constant by exiting or special handling (return a sentinel)
        // Here we just exit to catch this early
        exit(1);
    }

    if (node->type == NODE_BINARY_EXPR)
    {
        if (!node->left || !node->right)
        {
            fprintf(stderr, "Error: Binary expression missing operands\n");
            exit(1);
        }

        int left = evaluate_constant_expression(node->left);
        int right = evaluate_constant_expression(node->right);

        const char *op = node->value.str_val;
        if (!op)
        {
            fprintf(stderr, "Error: Binary expression without operator string\n");
            exit(1);
        }

        if (strcmp(op, "+") == 0)
            return left + right;
        if (strcmp(op, "-") == 0)
            return left - right;
        if (strcmp(op, "*") == 0)
            return left * right;
        if (strcmp(op, "/") == 0)
        {
            if (right == 0)
            {
                fprintf(stderr, "Error: Division by zero\n");
                exit(1);
            }
            return left / right;
        }
        if (strcmp(op, "%") == 0)
        {
            if (right == 0)
            {
                fprintf(stderr, "Error: Modulo by zero\n");
                exit(1);
            }
            return left % right;
        }
        if (strcmp(op, "&") == 0)
            return left & right;
        if (strcmp(op, "|") == 0)
            return left | right;
        if (strcmp(op, "^") == 0)
            return left ^ right;
        if (strcmp(op, "<<") == 0)
            return left << right;
        if (strcmp(op, ">>") == 0)
            return left >> right;
        if (strcmp(op, "==") == 0)
            return left == right;
        if (strcmp(op, "<") == 0)
            return left < right;
        if (strcmp(op, "<=") == 0)
            return left <= right;
        if (strcmp(op, ">") == 0)
            return left > right;
        if (strcmp(op, ">=") == 0)
            return left >= right;
        if (strcmp(op, "!=") == 0)
            return left != right;
        if (strcmp(op, "&&") == 0)
            return left && right;
        if (strcmp(op, "||") == 0)
            return left || right;

        fprintf(stderr, "Error: Unknown operator '%s'\n", op);
        exit(1);
    }

    fprintf(stderr, "Error: Cannot evaluate node type %d as constant\n",
            node->type);
    exit(1);
}

// Operator Precedence
static int get_precedence(const char *op)
{
    if (!op)
        return -1;

    if (strcmp(op, "*") == 0 || strcmp(op, "/") == 0 ||
        strcmp(op, "%") == 0)
        return 9;

    if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0)
        return 8;

    if (strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0)
        return 7;

    if (strcmp(op, "<") == 0 || strcmp(op, "<=") == 0 ||
        strcmp(op, ">") == 0 || strcmp(op, ">=") == 0)
        return 6;

    if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0)
        return 5;

    if (strcmp(op, "&") == 0)
        return 4;

    if (strcmp(op, "^") == 0)
        return 3;

    if (strcmp(op, "|") == 0)
        return 2;

    if (strcmp(op, "&&") == 0)
        return 1;

    if (strcmp(op, "||") == 0)
        return 0;

    // Unknown operator
    return -1;
}

// Primary Parser
static Node *parse_primary(Token *tokens, size_t *i, size_t num_tokens,
                           ScopeStack *scope_stack)
{
    if (*i >= num_tokens)
    {
        printf("Error: Unexpected end of input at line %d\n",
               tokens[*i - 1].line);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }

    Token token = tokens[*i];

    // Unary operators: !x, ~x, -x. Bind tighter than any binary operator.
    if (token.type == OPERATOR &&
        (strcmp(token.value.str_val, "!") == 0 ||
         strcmp(token.value.str_val, "~") == 0 ||
         strcmp(token.value.str_val, "-") == 0))
    {
        Token op = token;
        (*i)++;
        Node *operand = parse_primary(tokens, i, num_tokens, scope_stack);
        if (!operand)
        {
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }

        // Resolve operand to an integer if possible (literal or identifier).
        int have_int = 0;
        int operand_val = 0;
        if (operand->type == NODE_LITERAL_INT)
        {
            have_int = 1;
            operand_val = operand->value.int_val;
        }
        else if (operand->type == NODE_IDENTIFIER)
        {
            Symbol *sym = find_symbol_in_scope_stack(scope_stack,
                                                     operand->value.str_val);
            if (sym)
            {
                have_int = 1;
                operand_val = sym->value;
            }
        }

        if (have_int)
        {
            int result;
            if (strcmp(op.value.str_val, "!") == 0)
                result = !operand_val;
            else if (strcmp(op.value.str_val, "~") == 0)
                result = ~operand_val;
            else
                result = -operand_val;

            free_ast(operand);
            Node *folded = createNode(NODE_LITERAL_INT, NULL, op.line, op.col);
            if (!folded)
            {
                free_scope_stack(scope_stack);
                free_tokens(tokens, num_tokens);
                exit(1);
            }
            folded->value.int_val = result;
            return folded;
        }

        // Fallback: keep as binary expression with 0 as left operand for
        // unary minus, or leave unfolded. For toycc, identifiers always
        // resolve, so this path is rarely hit.
        return operand;
    }

    if (token.type == INT || token.type == IDENTIFIER)
    {
        (*i)++;
        Node *node = createNodeFromToken(token);
        if (!node)
        {
            printf("Error: Failed to create node at line %d\n", token.line);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }

        if (token.type == IDENTIFIER)
        {
            Symbol *sym = find_symbol_in_scope_stack(scope_stack,
                                                     token.value.str_val);
            if (!sym)
            {
                error_at(token.line, token.col,
                         "undefined variable '%s'", token.value.str_val);
            }
            if (sym->type == VAR_INT)
            {
                Node *constant_node = createNode(NODE_LITERAL_INT, NULL,
                                                 token.line, token.col);
                if (!constant_node)
                {
                    free_ast(node);
                    free_scope_stack(scope_stack);
                    free_tokens(tokens, num_tokens);
                    exit(1);
                }
                constant_node->value.int_val = sym->value;
                free_ast(node);
                return constant_node;
            }
        }
        return node;
    }

    if (token.type == SEPARATOR && strcmp(token.value.str_val, "(") == 0)
    {
        (*i)++;
        Node *expr = parse_expression(tokens, i, num_tokens, scope_stack, 0);

        if (*i >= num_tokens || strcmp(tokens[*i].value.str_val, ")") != 0)
        {
            printf("Error: Expected ')' at line %d\n", tokens[*i].line);
            free_ast(expr); // Free the expression before exiting
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
        (*i)++;
        return expr;
    }

    printf("Error: Unexpected token '%s' at line %d\n", token.value.str_val,
           token.line);
    free_scope_stack(scope_stack);
    free_tokens(tokens, num_tokens);
    exit(1);
}

// Expression Parser
Node *parse_expression(Token *tokens, size_t *i, size_t num_tokens,
                       ScopeStack *scope_stack, int min_precedence)
{
    Node *left = parse_primary(tokens, i, num_tokens, scope_stack);
    if (!left)
        return NULL;

    while (*i < num_tokens)
    {
        Token op_token = tokens[*i];
        if (op_token.type != OPERATOR)
            break;

        const char *op = op_token.value.str_val;
        int precedence = get_precedence(op);
        if (precedence < min_precedence)
            break;

        (*i)++;
        Node *right = parse_expression(tokens, i, num_tokens, scope_stack,
                                       precedence + 1);
        if (!right)
        {
            free_ast(left);
            return NULL;
        }

        Node *binary_expr = createNodeFromToken(op_token);
        if (!binary_expr)
        {
            free_ast(left);
            free_ast(right);
            return NULL;
        }
        binary_expr->left = left;
        binary_expr->right = right;

        // Fold only if both left and right are literals
        if (left->type == NODE_LITERAL_INT && right->type == NODE_LITERAL_INT)
        {
            int result = evaluate_constant_expression(binary_expr);

            Node *folded = createNode(NODE_LITERAL_INT, NULL,
                                      op_token.line, op_token.col);
            if (!folded)
            {
                free_ast(binary_expr); // This will free left and right too
                return NULL;
            }
            folded->value.int_val = result;

            // Detach children before freeing to avoid double free
            binary_expr->left = NULL;
            binary_expr->right = NULL;
            free_ast(binary_expr);
            free_ast(left);
            free_ast(right);

            left = folded;
        }
        else
        {
            // Keep binary expression node if folding not possible
            left = binary_expr;
        }
    }

    // Ternary: cond ? then_expr : else_expr. Only at top level
    // (min_precedence == 0) so it sits at the bottom of the precedence
    // ladder. Right-associative by calling parse_expression recursively.
    if (min_precedence == 0 &&
        *i < num_tokens &&
        tokens[*i].type == OPERATOR &&
        strcmp(tokens[*i].value.str_val, "?") == 0)
    {
        (*i)++; // consume '?'

        Node *then_expr = parse_expression(tokens, i, num_tokens,
                                           scope_stack, 0);
        if (!then_expr)
        {
            free_ast(left);
            return NULL;
        }

        if (*i >= num_tokens ||
            tokens[*i].type != OPERATOR ||
            strcmp(tokens[*i].value.str_val, ":") != 0)
        {
            printf("Error: Expected ':' in ternary at line %d\n",
                   tokens[*i >= num_tokens ? *i - 1 : *i].line);
            free_ast(left);
            free_ast(then_expr);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
        (*i)++; // consume ':'

        Node *else_expr = parse_expression(tokens, i, num_tokens,
                                           scope_stack, 0);
        if (!else_expr)
        {
            free_ast(left);
            free_ast(then_expr);
            return NULL;
        }

        // Resolve condition if possible; pick branch at parse time.
        int cond_known = 0;
        int cond_val = 0;
        if (left->type == NODE_LITERAL_INT)
        {
            cond_known = 1;
            cond_val = left->value.int_val;
        }
        else if (left->type == NODE_IDENTIFIER)
        {
            Symbol *sym = find_symbol_in_scope_stack(scope_stack,
                                                     left->value.str_val);
            if (sym)
            {
                cond_known = 1;
                cond_val = sym->value;
            }
        }

        if (cond_known)
        {
            Node *picked = cond_val ? then_expr : else_expr;
            Node *discarded = cond_val ? else_expr : then_expr;
            free_ast(left);
            free_ast(discarded);
            return picked;
        }

        // Non-constant condition: just return then_expr as a best-effort.
        // (Shouldn't happen in toycc's partial-evaluator model.)
        free_ast(left);
        free_ast(else_expr);
        return then_expr;
    }

    return left;
}

// Variable Declaration Parser
static Node *parse_variable_declaration(Token *tokens, size_t *i,
                                        size_t num_tokens,
                                        ScopeStack *scope_stack,
                                        Node **last_decl_out,
                                        bool condition_active)
{
    // Optional 'const' prefix — binds to all identifiers declared here.
    bool is_const = false;
    if (*i < num_tokens && tokens[*i].type == KEYWORD &&
        strcmp(tokens[*i].value.str_val, "const") == 0)
    {
        is_const = true;
        (*i)++;
    }

    if (*i >= num_tokens || tokens[*i].type != KEYWORD ||
        (strcmp(tokens[*i].value.str_val, "int") != 0 &&
         strcmp(tokens[*i].value.str_val, "char") != 0))
    {
        printf("Error: Expected 'int' or 'char' keyword at line %d\n",
               tokens[*i].line);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    (*i)++; // consume type keyword (both int and char store as VAR_INT)

    Node *first_decl = NULL;
    Node *current_decl = NULL;
    SymbolTable *current_table = current_scope(scope_stack);

    while (*i < num_tokens)
    {
        if (tokens[*i].type != IDENTIFIER)
        {
            printf("Error: Expected identifier at line %d\n", tokens[*i].line);
            free_ast(first_decl); // Clean up partial AST
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
        Token id_token = tokens[*i];
        (*i)++;

        Node *init_expr = NULL;
        int initial_value = 0;
        bool has_initializer = false;

        if (*i < num_tokens && strcmp(tokens[*i].value.str_val, "=") == 0)
        {
            (*i)++;
            has_initializer = true;
            init_expr = parse_expression(tokens, i, num_tokens, scope_stack, 0);
            if (!init_expr)
            {
                free_ast(first_decl);
                free_scope_stack(scope_stack);
                free_tokens(tokens, num_tokens);
                exit(1);
            }

            if (init_expr->type == NODE_LITERAL_INT)
            {
                initial_value = init_expr->value.int_val;
            }
            else if (init_expr->type == NODE_IDENTIFIER)
            {
                Symbol *sym =
                    find_symbol_in_scope_stack(scope_stack,
                                               init_expr->value.str_val);
                if (sym)
                    initial_value = sym->value;
            }
        }

        if (is_const && !has_initializer)
        {
            error_at(id_token.line, id_token.col,
                     "'const' variable '%s' requires an initializer",
                     id_token.value.str_val);
        }

        // Only add symbol if the condition is active
        if (condition_active)
        {
            add_symbol(current_table, id_token.value.str_val, VAR_INT,
                       initial_value, id_token.line, id_token.col);
            if (is_const)
            {
                // Mark the symbol we just added as const.
                current_table->symbols[current_table->size - 1].is_const = 1;
            }
        }

        Node *decl_node = createNode(NODE_VAR_DECL, id_token.value.str_val,
                                     id_token.line, id_token.col);
        if (!decl_node)
        {
            free_ast(init_expr);
            free_ast(first_decl);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
        decl_node->left = init_expr;

        if (!first_decl)
        {
            first_decl = decl_node;
            current_decl = decl_node;
        }
        else
        {
            current_decl->right = decl_node;
            current_decl = decl_node;
        }

        if (*i >= num_tokens)
        {
            printf("Error: Unexpected end of input\n");
            free_ast(first_decl);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }

        if (strcmp(tokens[*i].value.str_val, ";") == 0)
        {
            (*i)++;
            break;
        }
        else if (strcmp(tokens[*i].value.str_val, ",") == 0)
        {
            (*i)++;
        }
        else
        {
            printf("Error: Expected ',' or ';' at line %d\n", tokens[*i].line);
            free_ast(first_decl);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
    }

    if (last_decl_out)
        *last_decl_out = current_decl;
    return first_decl;
}

// Assignment Statement Parser
static Node *parse_assignment_statement(Token *tokens, size_t *i,
                                        size_t num_tokens,
                                        ScopeStack *scope_stack,
                                        bool condition_active)
{
    int start_line = tokens[*i].line;
    int start_col = tokens[*i].col;

    if (*i >= num_tokens || tokens[*i].type != IDENTIFIER)
    {
        printf("Error: Expected identifier at line %d\n", tokens[*i].line);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    Token id_token = tokens[*i];
    (*i)++;

    // Find which table contains the variable
    SymbolTable *target_table = NULL;
    for (int j = scope_stack->size - 1; j >= 0; j--)
    {
        if (find_symbol(scope_stack->tables[j], id_token.value.str_val))
        {
            target_table = scope_stack->tables[j];
            break;
        }
    }
    if (!target_table)
    {
        error_at(id_token.line, id_token.col,
                 "undefined variable '%s'", id_token.value.str_val);
    }

    // Refuse to assign to a const. Check once here, before parsing RHS.
    {
        Symbol *target = find_symbol(target_table, id_token.value.str_val);
        if (target && target->is_const)
        {
            error_at(id_token.line, id_token.col,
                     "assignment to const variable '%s'",
                     id_token.value.str_val);
        }
    }

    if (*i >= num_tokens || tokens[*i].type != OPERATOR ||
        (strcmp(tokens[*i].value.str_val, "=") != 0 &&
         strcmp(tokens[*i].value.str_val, "+=") != 0 &&
         strcmp(tokens[*i].value.str_val, "-=") != 0 &&
         strcmp(tokens[*i].value.str_val, "*=") != 0 &&
         strcmp(tokens[*i].value.str_val, "/=") != 0 &&
         strcmp(tokens[*i].value.str_val, "%=") != 0 &&
         strcmp(tokens[*i].value.str_val, "<<=") != 0 &&
         strcmp(tokens[*i].value.str_val, ">>=") != 0))
    {
        printf("Error: Expected assignment operator at line %d\n",
               tokens[*i].line);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }

    Token op_token = tokens[*i];
    (*i)++;

    Node *expr = parse_expression(tokens, i, num_tokens, scope_stack, 0);
    if (!expr)
    {
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }

    if (*i >= num_tokens || strcmp(tokens[*i].value.str_val, ";") != 0)
    {
        printf("Error: Expected ';' at line %d\n", tokens[*i - 1].line);
        free_ast(expr);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    (*i)++;

    // Create assignment node
    Node *assign_node = createNode(NODE_ASSIGNMENT, "=", start_line, start_col);
    if (!assign_node)
    {
        free_ast(expr);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }

    // Create left-hand side identifier node
    Node *lhs = createNode(NODE_IDENTIFIER, id_token.value.str_val,
                           id_token.line, id_token.col);
    if (!lhs)
    {
        free_ast(assign_node);
        free_ast(expr);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    assign_node->left = lhs;

    Node *value_to_store = expr;

    // Handle compound assignment operators
    if (strcmp(op_token.value.str_val, "=") != 0)
    {
        // Extract the binary operator (remove the '=' at the end)
        char op[4] = {0};
        strncpy(op, op_token.value.str_val, strlen(op_token.value.str_val) - 1);

        // Create a new identifier node for the binary operation
        // (don't reuse lhs)
        Node *lhs_copy = createNode(NODE_IDENTIFIER, id_token.value.str_val,
                                    id_token.line, id_token.col);
        if (!lhs_copy)
        {
            free_ast(assign_node); // This will free lhs too
            free_ast(expr);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }

        Node *bin_op = createNode(NODE_BINARY_EXPR, op,
                                  op_token.line, op_token.col);
        if (!bin_op)
        {
            free_ast(assign_node);
            free_ast(lhs_copy);
            free_ast(expr);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
        bin_op->left = lhs_copy;
        bin_op->right = expr;
        value_to_store = bin_op;
    }

    // AST shape: assign_node->left = lhs, lhs->right = value_to_store.
    // This keeps assign_node->right free to act as the LCRS sibling
    // pointer for the enclosing block, avoiding aliasing between
    // "RHS of assignment" and "next statement".
    lhs->right = value_to_store;

    // Only update symbol if the condition is active
    if (condition_active && target_table)
    {
        Symbol *sym = find_symbol(target_table, id_token.value.str_val);
        int current_value = sym ? sym->value : 0;

        if (value_to_store->type == NODE_LITERAL_INT)
        {
            update_symbol(target_table, id_token.value.str_val,
                          value_to_store->value.int_val);
        }
        else if (value_to_store->type == NODE_IDENTIFIER)
        {
            Symbol *src_sym =
                find_symbol_in_scope_stack(scope_stack,
                                           value_to_store->value.str_val);
            if (src_sym)
            {
                update_symbol(target_table, id_token.value.str_val,
                              src_sym->value);
            }
        }
        else if (value_to_store->type == NODE_BINARY_EXPR)
        {
            int rhs_value = 0;

            if (expr->type == NODE_LITERAL_INT)
            {
                rhs_value = expr->value.int_val;
            }
            else if (expr->type == NODE_IDENTIFIER)
            {
                Symbol *rhs_sym = find_symbol_in_scope_stack(scope_stack,
                                                             expr->value.str_val);
                if (rhs_sym)
                    rhs_value = rhs_sym->value;
            }

            const char *op = value_to_store->value.str_val;
            int result = current_value;

            if (op && strcmp(op, "+") == 0)
                result = current_value + rhs_value;
            else if (op && strcmp(op, "-") == 0)
                result = current_value - rhs_value;
            else if (op && strcmp(op, "*") == 0)
                result = current_value * rhs_value;
            else if (op && strcmp(op, "/") == 0 && rhs_value != 0)
                result = current_value / rhs_value;
            else if (op && strcmp(op, "%") == 0 && rhs_value != 0)
                result = current_value % rhs_value;
            else if (op && strcmp(op, "<<") == 0)
                result = current_value << rhs_value;
            else if (op && strcmp(op, ">>") == 0)
                result = current_value >> rhs_value;

            update_symbol(target_table, id_token.value.str_val, result);

            // After evaluating the binary expression for symbol table update,
            // we can optionally fold it into a literal to save memory
            // and simplify the AST
            Node *literal_result = createNode(NODE_LITERAL_INT, NULL,
                                              value_to_store->line,
                                              value_to_store->col);
            if (!literal_result)
            {
                // Don't exit here, just keep the original binary expression
                // The assignment is still valid
            }
            else
            {
                literal_result->value.int_val = result;

                // Properly free the binary expression and its children
                free_ast(value_to_store);
                lhs->right = literal_result;
            }
        }
    }

    return assign_node;
}

// Exit Statement Parser
static Node *parse_exit_statement(Token *tokens, size_t *i, size_t num_tokens,
                                  ScopeStack *scope_stack, OutputLog *log,
                                  bool condition_active)
{
    int start_line = tokens[*i].line;
    int start_col = tokens[*i].col;

    Node *exit_node = createNode(NODE_EXIT_CALL, "exit", start_line, start_col);
    if (!exit_node)
    {
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    (*i)++;

    if (*i >= num_tokens || strcmp(tokens[*i].value.str_val, "(") != 0)
    {
        printf("Error: Expected '(' after 'exit' at line %d\n",
               tokens[*i - 1].line);
        free_ast(exit_node);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    (*i)++;

    Node *arg = parse_expression(tokens, i, num_tokens, scope_stack, 0);
    if (!arg)
    {
        free_ast(exit_node);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }

    if (arg->type == NODE_IDENTIFIER)
    {
        Symbol *sym = find_symbol_in_scope_stack(scope_stack,
                                                 arg->value.str_val);
        if (!sym)
        {
            printf("Error: Undefined variable '%s' at line %d\n",
                   arg->value.str_val, arg->line);
            free_ast(arg);
            free_ast(exit_node);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
        if (sym->type != VAR_INT)
        {
            printf("Error: Variable '%s' is not an integer at line %d\n",
                   arg->value.str_val, arg->line);
            free_ast(arg);
            free_ast(exit_node);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
    }

    if (*i >= num_tokens || strcmp(tokens[*i].value.str_val, ")") != 0)
    {
        printf("Error: Expected ')' at line %d\n", tokens[*i - 1].line);
        free_ast(arg);
        free_ast(exit_node);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    (*i)++;

    if (*i >= num_tokens || strcmp(tokens[*i].value.str_val, ";") != 0)
    {
        printf("Error: Expected ';' at line %d\n", tokens[*i - 1].line);
        free_ast(arg);
        free_ast(exit_node);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    (*i)++;

    exit_node->left = arg;

    // Log the exit so codegen can emit it. Only if this path is live
    // (condition_active == true); dead branches never reach runtime.
    if (condition_active)
    {
        int code = 0;
        if (arg->type == NODE_LITERAL_INT)
        {
            code = arg->value.int_val;
        }
        else if (arg->type == NODE_IDENTIFIER)
        {
            Symbol *sym = find_symbol_in_scope_stack(scope_stack,
                                                     arg->value.str_val);
            if (sym)
                code = sym->value;
        }
        output_log_append_exit(log, code);
    }

    return exit_node;
}

// Integer to decimal ASCII, signed. Writes into out[]; returns length.
// out must hold at least 12 bytes (for -2147483648 + null terminator not
// required here, we return length only).
static size_t int_to_ascii(int value, char out[12])
{
    if (value == 0)
    {
        out[0] = '0';
        return 1;
    }

    char tmp[12];
    size_t t = 0;
    int negative = 0;
    unsigned int uv;
    if (value < 0)
    {
        negative = 1;
        uv = (unsigned int)(-(long long)value);
    }
    else
    {
        uv = (unsigned int)value;
    }
    while (uv > 0)
    {
        tmp[t++] = (char)('0' + (uv % 10));
        uv /= 10;
    }
    size_t len = 0;
    if (negative)
        out[len++] = '-';
    while (t > 0)
        out[len++] = tmp[--t];
    return len;
}

// printf("fmt" [, arg]*); — simulates the call and appends the
// final byte sequence to the output log (when condition_active).
static Node *parse_printf_statement(Token *tokens, size_t *i,
                                    size_t num_tokens,
                                    ScopeStack *scope_stack,
                                    OutputLog *log,
                                    bool condition_active)
{
    int start_line = tokens[*i].line;
    int start_col = tokens[*i].col;

    Node *printf_node = createNode(NODE_PRINTF_CALL, "printf",
                                   start_line, start_col);
    if (!printf_node)
    {
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    (*i)++; // consume 'printf'

    if (*i >= num_tokens ||
        tokens[*i].type != SEPARATOR ||
        strcmp(tokens[*i].value.str_val, "(") != 0)
    {
        printf("Error: Expected '(' after 'printf' at line %d\n",
               tokens[*i - 1].line);
        free_ast(printf_node);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    (*i)++;

    // Format string must be a string literal.
    if (*i >= num_tokens || tokens[*i].type != STRING_LITERAL)
    {
        printf("Error: printf requires a string literal format at line %d\n",
               tokens[*i].line);
        free_ast(printf_node);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    Token fmt_tok = tokens[*i];
    (*i)++;

    // Build final bytes in a growable buffer.
    size_t cap = fmt_tok.str_len + 16;
    size_t len = 0;
    char *out = malloc(cap);
    if (!out)
    {
        fprintf(stderr, "printf: malloc failed\n");
        free_ast(printf_node);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }

#define ENSURE(n)                                                 \
    do                                                            \
    {                                                             \
        while (len + (n) > cap)                                   \
        {                                                         \
            cap *= 2;                                             \
            char *nb = realloc(out, cap);                         \
            if (!nb)                                              \
            {                                                     \
                fprintf(stderr, "printf: realloc failed\n");      \
                free(out);                                        \
                free_ast(printf_node);                            \
                free_scope_stack(scope_stack);                    \
                free_tokens(tokens, num_tokens);                  \
                exit(1);                                          \
            }                                                     \
            out = nb;                                             \
        }                                                         \
    } while (0)

    // Walk the format string, consuming an argument for each conversion.
    for (size_t p = 0; p < fmt_tok.str_len; p++)
    {
        char c = fmt_tok.value.str_val[p];
        if (c != '%')
        {
            ENSURE(1);
            out[len++] = c;
            continue;
        }
        if (p + 1 >= fmt_tok.str_len)
        {
            printf("Error: trailing '%%' in format at line %d\n",
                   fmt_tok.line);
            free(out);
            free_ast(printf_node);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
        char spec = fmt_tok.value.str_val[++p];

        if (spec == '%')
        {
            ENSURE(1);
            out[len++] = '%';
            continue;
        }

        // Need a comma and an argument for %d and %s.
        if (*i >= num_tokens ||
            tokens[*i].type != SEPARATOR ||
            strcmp(tokens[*i].value.str_val, ",") != 0)
        {
            printf("Error: printf: missing argument for '%%%c' at line %d\n",
                   spec, fmt_tok.line);
            free(out);
            free_ast(printf_node);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
        (*i)++; // consume ','

        if (spec == 'd')
        {
            Node *arg = parse_expression(tokens, i, num_tokens,
                                         scope_stack, 0);
            if (!arg)
            {
                free(out);
                free_ast(printf_node);
                free_scope_stack(scope_stack);
                free_tokens(tokens, num_tokens);
                exit(1);
            }
            int val = 0;
            if (arg->type == NODE_LITERAL_INT)
            {
                val = arg->value.int_val;
            }
            else if (arg->type == NODE_IDENTIFIER)
            {
                Symbol *sym = find_symbol_in_scope_stack(
                    scope_stack, arg->value.str_val);
                if (!sym)
                {
                    printf("Error: Undefined variable '%s' at line %d\n",
                           arg->value.str_val, arg->line);
                    free_ast(arg);
                    free(out);
                    free_ast(printf_node);
                    free_scope_stack(scope_stack);
                    free_tokens(tokens, num_tokens);
                    exit(1);
                }
                val = sym->value;
            }
            free_ast(arg);

            char digits[12];
            size_t dlen = int_to_ascii(val, digits);
            ENSURE(dlen);
            for (size_t d = 0; d < dlen; d++)
                out[len++] = digits[d];
        }
        else if (spec == 's')
        {
            if (*i >= num_tokens || tokens[*i].type != STRING_LITERAL)
            {
                printf("Error: printf %%s requires a string literal "
                       "argument at line %d\n",
                       tokens[*i].line);
                free(out);
                free_ast(printf_node);
                free_scope_stack(scope_stack);
                free_tokens(tokens, num_tokens);
                exit(1);
            }
            Token s = tokens[*i];
            (*i)++;
            ENSURE(s.str_len);
            for (size_t k = 0; k < s.str_len; k++)
                out[len++] = s.value.str_val[k];
        }
        else if (spec == 'c')
        {
            // %c takes an int argument and emits a single byte.
            Node *arg = parse_expression(tokens, i, num_tokens,
                                         scope_stack, 0);
            if (!arg)
            {
                free(out);
                free_ast(printf_node);
                free_scope_stack(scope_stack);
                free_tokens(tokens, num_tokens);
                exit(1);
            }
            int val = 0;
            if (arg->type == NODE_LITERAL_INT)
            {
                val = arg->value.int_val;
            }
            else if (arg->type == NODE_IDENTIFIER)
            {
                Symbol *sym = find_symbol_in_scope_stack(
                    scope_stack, arg->value.str_val);
                if (!sym)
                {
                    printf("Error: Undefined variable '%s' at line %d\n",
                           arg->value.str_val, arg->line);
                    free_ast(arg);
                    free(out);
                    free_ast(printf_node);
                    free_scope_stack(scope_stack);
                    free_tokens(tokens, num_tokens);
                    exit(1);
                }
                val = sym->value;
            }
            free_ast(arg);
            ENSURE(1);
            out[len++] = (char)(val & 0xFF);
        }
        else
        {
            printf("Error: printf: unsupported conversion '%%%c' at line %d\n",
                   spec, fmt_tok.line);
            free(out);
            free_ast(printf_node);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
    }
#undef ENSURE

    // Expect ')'
    if (*i >= num_tokens ||
        tokens[*i].type != SEPARATOR ||
        strcmp(tokens[*i].value.str_val, ")") != 0)
    {
        printf("Error: Expected ')' after printf args at line %d\n",
               tokens[*i - 1].line);
        free(out);
        free_ast(printf_node);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    (*i)++;

    // Expect ';'
    if (*i >= num_tokens ||
        tokens[*i].type != SEPARATOR ||
        strcmp(tokens[*i].value.str_val, ";") != 0)
    {
        printf("Error: Expected ';' after printf(...) at line %d\n",
               tokens[*i - 1].line);
        free(out);
        free_ast(printf_node);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    (*i)++;

    if (condition_active && len > 0)
    {
        output_log_append_write(log, out, len);
    }
    free(out);
    return printf_node;
}

// AST Freeing
void free_ast(Node *node)
{
    if (!node)
        return;

    free_ast(node->left);
    free_ast(node->right);

    // Only free string if it's not a literal int and string exists
    if (node->type != NODE_LITERAL_INT && node->value.str_val)
    {
        free(node->value.str_val);
        node->value.str_val = NULL;
    }

    free(node);
}

// Tree Traversal
void treeTraversal(Node *node, int depth)
{
    while (node)
    {
        for (int i = 0; i < depth; i++)
            printf("  ");

        switch (node->type)
        {
        case NODE_BEGIN:
            printf("PROGRAM\n");
            treeTraversal(node->left, depth + 1);
            break;

        case NODE_VAR_DECL:
            printf("VAR_DECL: %s\n",
                   node->value.str_val ? node->value.str_val : "(null)");
            treeTraversal(node->left, depth + 1);
            break;

        case NODE_ASSIGNMENT:
            printf("ASSIGNMENT: %s\n",
                   node->value.str_val ? node->value.str_val : "(null)");
            treeTraversal(node->left, depth + 1);
            break;

        case NODE_BINARY_EXPR:
            printf("BINARY_EXPR: %s\n",
                   node->value.str_val ? node->value.str_val : "(null)");
            treeTraversal(node->left, depth + 1);
            treeTraversal(node->right, depth + 1);
            return; // Skip sibling traversal here

        case NODE_EXIT_CALL:
            printf("EXIT_CALL\n");
            treeTraversal(node->left, depth + 1);
            break;

        case NODE_PRINTF_CALL:
            printf("PRINTF_CALL\n");
            break;

        case NODE_BREAK:
            printf("BREAK\n");
            break;

        case NODE_CONTINUE:
            printf("CONTINUE\n");
            break;

        case NODE_IF_STATEMENT:
            printf("IF_STATEMENT\n");

            for (int i = 0; i < depth + 1; i++)
                printf("  ");
            printf("CONDITION:\n");
            treeTraversal(node->left, depth + 2);
            break;

        case NODE_ELSE_IF_STATEMENT:
            printf("ELSE_IF_STATEMENT\n");

            for (int i = 0; i < depth + 1; i++)
                printf("  ");
            printf("CONDITION:\n");
            treeTraversal(node->left, depth + 2);
            break;

        case NODE_ELSE_STATEMENT:
            printf("ELSE_STATEMENT\n");
            treeTraversal(node->left, depth + 2);
            break;

        case NODE_WHILE_STATEMENT:
            printf("WHILE_STATEMENT\n");

            for (int i = 0; i < depth + 1; i++)
                printf("  ");
            printf("CONDITION:\n");
            treeTraversal(node->left, depth + 2);
            break;

        case NODE_FOR_STATEMENT:
            printf("FOR_STATEMENT\n");
            treeTraversal(node->left, depth + 2);
            break;

        case NODE_DO_WHILE_STATEMENT:
            printf("DO_WHILE_STATEMENT\n");

            // for (int i = 0; i < depth + 1; i++)
            //     printf("  ");
            // printf("BLOCK:\n");
            treeTraversal(node->left, depth + 2);

            if (node->left && node->left->right)
            {
                for (int i = 0; i < depth + 1; i++)
                    printf("  ");
                printf("CONDITION:\n");
                treeTraversal(node->left->right, depth + 2);
            }
            break;

        case NODE_BLOCK:
            printf("BLOCK {\n");
            treeTraversal(node->left, depth + 1);
            for (int i = 0; i < depth; i++)
                printf("  ");
            printf("} // END BLOCK\n");
            break;

        case NODE_IDENTIFIER:
            printf("IDENTIFIER: %s\n",
                   node->value.str_val ? node->value.str_val : "(null)");
            break;

        case NODE_LITERAL_INT:
            printf("LITERAL_INT: %d\n", node->value.int_val);
            break;

        default:
            printf("[UNKNOWN NODE TYPE %d]\n", node->type);
            treeTraversal(node->left, depth + 1);
            break;
        }

        node = node->right;
    }
}

static Node *parse_if_statement(Token *tokens, size_t *i, size_t num_tokens,
                                ScopeStack *scope_stack, OutputLog *log,
                                LoopControl *lc,
                                Node **last_node_out,
                                bool outer_condition_active)
{
    int start_line = tokens[*i].line;
    int start_col = tokens[*i].col;

    // Consume 'if' keyword
    (*i)++;

    // Check for opening parenthesis
    if (*i >= num_tokens || strcmp(tokens[*i].value.str_val, "(") != 0)
    {
        printf("Error: Expected '(' after 'if' at line %d\n", tokens[*i].line);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    (*i)++;

    // Parse condition
    Node *condition = parse_expression(tokens, i, num_tokens, scope_stack, 0);
    if (!condition)
    {
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    debugPrintNode("After parse_expression - condition", condition);
    printf("Condition parsed. Current token: %s\n", tokens[*i].value.str_val);

    // Check for closing parenthesis
    if (*i >= num_tokens || strcmp(tokens[*i].value.str_val, ")") != 0)
    {
        printf("Error: Expected ')' after if condition at line %d\n",
               tokens[*i].line);
        free_ast(condition);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    (*i)++;

    // Evaluate the condition to determine if we should execute the block
    bool condition_active = true;
    if (condition->type == NODE_LITERAL_INT)
    {
        condition_active = condition->value.int_val != 0;
    }
    else if (condition->type == NODE_IDENTIFIER)
    {
        Symbol *sym = find_symbol_in_scope_stack(scope_stack,
                                                 condition->value.str_val);
        if (sym)
        {
            condition_active = sym->value != 0;
        }
    }

    // Parse then block with the condition status. Gate on outer scope too:
    // a nested if inside a dead branch must stay dead.
    Node *then_block = parse_block(tokens, i, num_tokens, scope_stack, log,
                                   lc,
                                   outer_condition_active && condition_active);
    if (!then_block)
    {
        free_ast(condition);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    printf("Then block parsed. Current token: %s\n", tokens[*i].value.str_val);

    // Create if node with proper structure
    Node *if_node = createNode(NODE_IF_STATEMENT, "if", start_line, start_col);
    if (!if_node)
    {
        free_ast(condition);
        free_ast(then_block);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }

    if_node->left = condition;
    condition->right = then_block; // Then-block is sibling of condition

    if (last_node_out)
        *last_node_out = if_node;

    printf(
        "DEBUG IF_SUBTREE: if_node=%p, cond=%p, then_block=%p, "
        "then_block->right=%p\n",
        (void *)if_node, (void *)condition, (void *)then_block,
        (void *)(then_block ? then_block->right : NULL));

    return if_node;
}

static Node *parse_else_if_statements(Token *tokens, size_t *i,
                                      size_t num_tokens,
                                      ScopeStack *scope_stack, OutputLog *log,
                                      LoopControl *lc,
                                      Node *if_node,
                                      bool outer_condition_active,
                                      bool prev_condition_active,
                                      Node **last_else_if_out)
{
    Node *last_else_if = NULL;
    bool any_condition_active = prev_condition_active;

    // Keep parsing else if statements as long as we find them
    while (*i < num_tokens &&
           tokens[*i].type == KEYWORD &&
           strcmp(tokens[*i].value.str_val, "else") == 0 &&
           *i + 1 < num_tokens &&
           tokens[*i + 1].type == KEYWORD &&
           strcmp(tokens[*i + 1].value.str_val, "if") == 0)
    {
        int start_line = tokens[*i].line;
        int start_col = tokens[*i].col;

        // Consume 'else if' keywords
        (*i)++; // 'else'
        (*i)++; // 'if'

        // Check for opening parenthesis
        if (*i >= num_tokens || strcmp(tokens[*i].value.str_val, "(") != 0)
        {
            printf("Error: Expected '(' after 'else if' at line %d\n",
                   tokens[*i].line);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
        (*i)++;

        // Parse condition
        Node *condition = parse_expression(tokens, i, num_tokens,
                                           scope_stack, 0);
        if (!condition)
        {
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
        debugPrintNode("After parse_expression - else if condition", condition);

        // Check for closing parenthesis
        if (*i >= num_tokens || strcmp(tokens[*i].value.str_val, ")") != 0)
        {
            printf("Error: Expected ')' after else if condition at line %d\n",
                   tokens[*i].line);
            free_ast(condition);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
        (*i)++;

        // Evaluate the condition - only active if no previous
        // condition was true
        bool condition_active = !any_condition_active;
        if (condition_active)
        {
            if (condition->type == NODE_LITERAL_INT)
            {
                condition_active = condition->value.int_val != 0;
            }
            else if (condition->type == NODE_IDENTIFIER)
            {
                Symbol *sym = find_symbol_in_scope_stack(scope_stack,
                                                         condition->value.str_val);
                if (sym)
                {
                    condition_active = sym->value != 0;
                }
            }
            any_condition_active = any_condition_active || condition_active;
        }

        // Parse then block. Gate on outer scope too.
        Node *else_if_block = parse_block(tokens, i, num_tokens,
                                          scope_stack, log, lc,
                                          outer_condition_active &&
                                              condition_active);
        if (!else_if_block)
        {
            free_ast(condition);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }

        // Create else if node
        Node *else_if_node = createNode(NODE_ELSE_IF_STATEMENT,
                                        "else if", start_line, start_col);
        if (!else_if_node)
        {
            free_ast(condition);
            free_ast(else_if_block);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
        else_if_node->left = condition;
        condition->right = else_if_block;

        // Link to the chain using last_else_if pointer
        if (!if_node->right)
        {
            if_node->right = else_if_node;
        }
        else
        {
            last_else_if->right = else_if_node;
        }
        last_else_if = else_if_node;
    }

    if (last_else_if_out)
    {
        *last_else_if_out = last_else_if;
    }
    return last_else_if;
}

static Node *parse_else_statement(Token *tokens, size_t *i, size_t num_tokens,
                                  ScopeStack *scope_stack, OutputLog *log,
                                  LoopControl *lc,
                                  Node **last_node_out,
                                  bool outer_condition_active)
{
    // First check if we're starting with 'else' without preceding 'if'
    if (tokens[*i].type == KEYWORD &&
        strcmp(tokens[*i].value.str_val, "else") == 0)
    {
        // Check if this is 'else if'
        if (*i + 1 < num_tokens &&
            tokens[*i + 1].type == KEYWORD &&
            strcmp(tokens[*i + 1].value.str_val, "if") == 0)
        {
            printf("Error at line %d:%d: 'else if' without preceding 'if'\n",
                   tokens[*i].line, tokens[*i].col);
        }
        else
        {
            printf("Error at line %d:%d: 'else' without preceding 'if'\n",
                   tokens[*i].line, tokens[*i].col);
        }
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }

    // Now parse the required if statement first
    Node *if_node = parse_if_statement(tokens, i, num_tokens,
                                       scope_stack, log, lc, NULL,
                                       outer_condition_active);
    if (!if_node)
    {
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    Node *last_node = if_node;

    // Track if any condition (if or else if) was true
    bool any_condition_active = false;
    if (if_node->left) // if condition
    {
        if (if_node->left->type == NODE_LITERAL_INT)
        {
            any_condition_active = if_node->left->value.int_val != 0;
        }
        else if (if_node->left->type == NODE_IDENTIFIER)
        {
            Symbol *sym = find_symbol_in_scope_stack(scope_stack,
                                                     if_node->left->value.str_val);
            if (sym)
            {
                any_condition_active = sym->value != 0;
            }
        }
    }

    // Now parse any else if statements and get the last one
    Node *last_else_if = NULL;
    parse_else_if_statements(tokens, i, num_tokens, scope_stack, log, lc,
                             if_node, outer_condition_active,
                             any_condition_active, &last_else_if);

    if (last_else_if)
    {
        last_node = last_else_if;
        // Update any_condition_active based on else if conditions
        Node *current = if_node->right;
        while (current)
        {
            if (current->type == NODE_ELSE_IF_STATEMENT && current->left)
            {
                if (current->left->type == NODE_LITERAL_INT)
                {
                    any_condition_active = any_condition_active ||
                                           (current->left->value.int_val != 0);
                }
                else if (current->left->type == NODE_IDENTIFIER)
                {
                    Symbol *sym = find_symbol_in_scope_stack(scope_stack,
                                                             current->left->value.str_val);
                    if (sym)
                    {
                        any_condition_active = any_condition_active ||
                                               (sym->value != 0);
                    }
                }
            }
            current = current->right;
        }
    }

    // Check for a final else clause
    if (*i < num_tokens &&
        tokens[*i].type == KEYWORD &&
        strcmp(tokens[*i].value.str_val, "else") == 0)
    {
        int start_line = tokens[*i].line;
        int start_col = tokens[*i].col;

        // Consume 'else' keyword
        (*i)++;

        // Else block is only active if no previous condition was true
        bool else_active = !any_condition_active;

        // Parse else block. Gate on outer scope.
        Node *else_block = parse_block(tokens, i, num_tokens, scope_stack, log,
                                       lc,
                                       outer_condition_active && else_active);
        if (!else_block)
        {
            free_ast(if_node);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }

        // Create else node
        Node *else_node = createNode(NODE_ELSE_STATEMENT,
                                     "else", start_line, start_col);
        if (!else_node)
        {
            free_ast(if_node);
            free_ast(else_block);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
        else_node->left = else_block;

        // Link to the chain using last_else_if pointer if available
        if (!if_node->right)
        {
            if_node->right = else_node;
        }
        else
        {
            // Use last_else_if if we have it, otherwise find the end
            Node *append_to = last_else_if ? last_else_if : if_node;
            while (append_to->right)
            {
                append_to = append_to->right;
            }
            append_to->right = else_node;
        }
        last_node = else_node;
    }

    if (last_node_out)
    {
        *last_node_out = last_node;
    }
    return if_node;
}

// DO-WHILE LOOP PARSER
Node *parse_do_while_statement(Token *tokens, size_t *i, size_t num_tokens,
                               ScopeStack *scope_stack, OutputLog *log,
                               bool outer_condition_active)
{
    int start_line = tokens[*i].line;
    int start_col = tokens[*i].col;

    (*i)++; // consume 'do'

    Node *do_while_node = createNode(NODE_DO_WHILE_STATEMENT,
                                     "do", start_line, start_col);
    if (!do_while_node)
    {
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }

    size_t block_start_pos = *i;
    bool condition_active = true;

    Node *first_condition = NULL;
    Node *first_block = NULL;
    bool first_iteration = true;

    int iteration_count = 0;
    LoopControl my_lc = {0, 0};

    // If we're inside a dead branch, skip simulation entirely and just
    // advance past the do-while construct.
    if (!outer_condition_active)
        goto skip_simulation;

    do
    {
        iteration_count++;
        if (iteration_count > MAX_LOOP_ITERATIONS)
        {
            fprintf(stderr,
                    "Error: do-while loop exceeded %d iterations at "
                    "line %d (likely infinite loop — runtime loops not "
                    "supported)\n",
                    MAX_LOOP_ITERATIONS, start_line);
            free_ast(do_while_node);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
        size_t temp_i = block_start_pos;

        printf("[DEBUG] Do-While Iteration %d: Parsing block "
               "at token index %zu\n",
               iteration_count, temp_i);

        // Parse block first (this is the key difference from while loop).
        // Reset continue flag per-iteration (it only affects *this* trip).
        my_lc.continuing = 0;
        Node *block = parse_block(tokens, &temp_i, num_tokens,
                                  scope_stack, log, &my_lc, condition_active);
        if (!block)
        {
            printf("Error: Failed to parse do-while block\n");
            free_ast(do_while_node);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }

        printf("[DEBUG] Block parsed, checking for 'while' "
               "keyword at token index %zu\n",
               temp_i);

        // Expect 'while' keyword after the block
        if (temp_i >= num_tokens)
        {
            printf("Error: Unexpected end of input, expected 'while' "
                   "after do block at line %d\n",
                   tokens[temp_i - 1].line);
            free_ast(block);
            free_ast(do_while_node);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }

        if (tokens[temp_i].type != KEYWORD ||
            strcmp(tokens[temp_i].value.str_val, "while") != 0)
        {
            printf("Error: Expected 'while' after do block at "
                   "line %d:%d, got '%s'\n",
                   tokens[temp_i].line, tokens[temp_i].col,
                   tokens[temp_i].value.str_val ? tokens[temp_i].value.str_val : "(null)");
            free_ast(block);
            free_ast(do_while_node);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
        temp_i++; // consume 'while'

        // Expect opening parenthesis
        if (temp_i >= num_tokens)
        {
            printf("Error: Unexpected end of input, expected '(' after "
                   "'while' at line %d\n",
                   tokens[temp_i - 1].line);
            free_ast(block);
            free_ast(do_while_node);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }

        if (strcmp(tokens[temp_i].value.str_val, "(") != 0)
        {
            printf("Error: Expected '(' after 'while' at "
                   "line %d:%d, got '%s'\n",
                   tokens[temp_i].line, tokens[temp_i].col,
                   tokens[temp_i].value.str_val ? tokens[temp_i].value.str_val : "(null)");
            free_ast(block);
            free_ast(do_while_node);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
        temp_i++; // consume '('

        printf("[DEBUG] Parsing condition at token index %zu\n", temp_i);

        Node *condition = parse_expression(tokens, &temp_i, num_tokens,
                                           scope_stack, 0);
        if (!condition)
        {
            printf("Error: Failed to parse do-while condition\n");
            free_ast(block);
            free_ast(do_while_node);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }

        printf("[DEBUG] Condition type: %d\n", condition->type);

        // Expect closing parenthesis
        if (temp_i >= num_tokens)
        {
            printf("Error: Unexpected end of input, expected ')' "
                   "after do-while condition at line %d\n",
                   tokens[temp_i - 1].line);
            free_ast(condition);
            free_ast(block);
            free_ast(do_while_node);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }

        if (strcmp(tokens[temp_i].value.str_val, ")") != 0)
        {
            printf("Error: Expected ')' after do-while condition "
                   "at line %d:%d, got '%s'\n",
                   tokens[temp_i].line, tokens[temp_i].col,
                   tokens[temp_i].value.str_val ? tokens[temp_i].value.str_val : "(null)");
            free_ast(condition);
            free_ast(block);
            free_ast(do_while_node);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
        temp_i++; // consume ')'

        // Expect semicolon
        if (temp_i >= num_tokens)
        {
            printf("Error: Unexpected end of input, expected ';' "
                   "after do-while statement at line %d\n",
                   tokens[temp_i - 1].line);
            free_ast(condition);
            free_ast(block);
            free_ast(do_while_node);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }

        if (strcmp(tokens[temp_i].value.str_val, ";") != 0)
        {
            printf("Error: Expected ';' after do-while statement at "
                   "line %d:%d, got '%s'\n",
                   tokens[temp_i].line, tokens[temp_i].col,
                   tokens[temp_i].value.str_val ? tokens[temp_i].value.str_val : "(null)");
            free_ast(condition);
            free_ast(block);
            free_ast(do_while_node);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
        temp_i++; // consume ';'

        // Evaluate condition with current symbol table state
        // (AFTER executing the block)
        if (condition->type == NODE_LITERAL_INT)
        {
            condition_active = condition->value.int_val != 0;
        }
        else if (condition->type == NODE_IDENTIFIER)
        {
            Symbol *sym = find_symbol_in_scope_stack(scope_stack,
                                                     condition->value.str_val);
            condition_active = sym ? (sym->value != 0) : false;
        }

        printf("[DEBUG] Condition active: %d\n", condition_active);

        if (first_iteration)
        {
            // Keep ONLY the first iteration nodes for AST/codegen
            first_block = block;
            first_condition = condition;
            first_iteration = false;
            printf("[DEBUG] Saved first iteration nodes for AST\n");
        }
        else
        {
            // Free subsequent iteration nodes - we only needed
            // their side effects on symbol table
            free_ast(block);
            free_ast(condition);
            printf("[DEBUG] Freed iteration %d nodes "
                   "(kept symbol table effects)\n",
                   iteration_count);
        }

        // If body executed a `break;`, exit the loop unconditionally.
        if (my_lc.breaking)
        {
            my_lc.breaking = 0;
            break;
        }

    } while (condition_active);

    // Link ONLY the first iteration nodes to the do_while_node AST
    // Structure: do_while_node->left = first_block,
    // first_block->right = first_condition
    do_while_node->left = first_block;
    if (first_block)
        first_block->right = first_condition;

skip_simulation:
    // Advance parser index past the entire do-while statement
    *i = block_start_pos;

    // Skip past block parsing. Pass a throwaway log (NULL) so re-parses
    // for index-advancing don't re-log output.
    Node *temp_block = parse_block(tokens, i, num_tokens, scope_stack, NULL,
                                   NULL, false);
    if (temp_block)
    {
        free_ast(temp_block); // Free the temporary block
    }

    // Skip past 'while' keyword
    if (*i < num_tokens && tokens[*i].type == KEYWORD &&
        strcmp(tokens[*i].value.str_val, "while") == 0)
    {
        (*i)++; // skip 'while'
    }

    // Skip past '('
    if (*i < num_tokens && tokens[*i].type == SEPARATOR &&
        strcmp(tokens[*i].value.str_val, "(") == 0)
    {
        (*i)++; // skip '('
    }

    // Skip past condition parsing
    Node *temp_condition = parse_expression(tokens, i, num_tokens,
                                            scope_stack, 0);
    if (temp_condition)
    {
        free_ast(temp_condition); // Free the temporary condition
    }

    // Skip past ')' and ';'
    if (*i < num_tokens && tokens[*i].type == SEPARATOR &&
        strcmp(tokens[*i].value.str_val, ")") == 0)
    {
        (*i)++; // skip ')'
    }
    if (*i < num_tokens && tokens[*i].type == SEPARATOR &&
        strcmp(tokens[*i].value.str_val, ";") == 0)
    {
        (*i)++; // skip ';'
    }

    printf("[DEBUG] Do-While loop completed: %d iterations unrolled, "
           "first iteration kept for AST\n",
           iteration_count);

    return do_while_node;
}

// WHILE LOOP PARSER
Node *parse_while_statement(Token *tokens, size_t *i, size_t num_tokens,
                            ScopeStack *scope_stack, OutputLog *log,
                            bool outer_condition_active)
{
    int start_line = tokens[*i].line;
    int start_col = tokens[*i].col;

    (*i)++; // consume 'while'

    if (*i >= num_tokens || strcmp(tokens[*i].value.str_val, "(") != 0)
    {
        printf("Error: Expected '(' after 'while' at line %d\n",
               tokens[*i].line);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    (*i)++; // consume '('

    Node *while_node = createNode(NODE_WHILE_STATEMENT, "while", start_line,
                                  start_col);
    if (!while_node)
    {
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }

    size_t loop_start_pos = *i;
    bool condition_active = true;

    Node *first_condition = NULL;
    Node *first_block = NULL;
    bool first_iteration = true;

    int iteration_count = 0;
    LoopControl my_lc = {0, 0};

    // If we're inside a dead branch, skip simulation entirely.
    if (!outer_condition_active)
        goto skip_simulation;

    while (condition_active)
    {
        iteration_count++;
        if (iteration_count > MAX_LOOP_ITERATIONS)
        {
            fprintf(stderr,
                    "Error: while loop exceeded %d iterations at line %d "
                    "(likely infinite loop — runtime loops not supported)\n",
                    MAX_LOOP_ITERATIONS, start_line);
            free_ast(while_node);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
        size_t temp_i = loop_start_pos;

        printf("[DEBUG] Iteration %d: Parsing condition at token index %zu\n",
               iteration_count, temp_i);

        Node *condition = parse_expression(tokens, &temp_i, num_tokens,
                                           scope_stack, 0);
        if (!condition)
        {
            printf("Error: Failed to parse while condition\n");
            free_ast(while_node);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }

        printf("[DEBUG] Condition type: %d\n", condition->type);

        if (temp_i >= num_tokens ||
            strcmp(tokens[temp_i].value.str_val, ")") != 0)
        {
            printf("Error: Expected ')' after while condition\n");
            free_ast(condition);
            free_ast(while_node);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
        temp_i++; // consume ')'

        // Evaluate condition with current symbol table state
        if (condition->type == NODE_LITERAL_INT)
        {
            condition_active = condition->value.int_val != 0;
        }
        else if (condition->type == NODE_IDENTIFIER)
        {
            Symbol *sym = find_symbol_in_scope_stack(scope_stack,
                                                     condition->value.str_val);
            condition_active = sym ? (sym->value != 0) : false;
        }

        printf("[DEBUG] Condition active: %d\n", condition_active);

        if (!condition_active)
        {
            // Condition false: free current condition and break
            free_ast(condition);
            break;
        }

        printf("[DEBUG] Parsing block starting at token index %zu\n", temp_i);

        // Parse block - this updates symbol table for semantic analysis.
        // Reset continue flag per-iteration.
        my_lc.continuing = 0;
        Node *block = parse_block(tokens, &temp_i, num_tokens, scope_stack,
                                  log, &my_lc, condition_active);
        if (!block)
        {
            printf("Error: Failed to parse while block\n");
            free_ast(condition);
            free_ast(while_node);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }

        if (first_iteration)
        {
            // Keep ONLY the first iteration nodes for AST/codegen
            first_condition = condition;
            first_block = block;
            first_iteration = false;
            printf("[DEBUG] Saved first iteration nodes for AST\n");
        }
        else
        {
            // Free subsequent iteration nodes - we only needed
            // their side effects on symbol table
            free_ast(condition);
            free_ast(block);
            printf("[DEBUG] Freed iteration %d nodes "
                   "(kept symbol table effects)\n",
                   iteration_count);
        }

        // If body executed a `break;`, exit the loop unconditionally.
        if (my_lc.breaking)
        {
            my_lc.breaking = 0;
            break;
        }
    }

    // Link ONLY the first iteration nodes to the while_node AST
    while_node->left = first_condition;
    if (first_condition)
        first_condition->right = first_block;

skip_simulation:
    // Advance parser index past the entire while statement
    *i = loop_start_pos;

    // Skip past condition parsing
    Node *temp_condition = parse_expression(tokens, i, num_tokens,
                                            scope_stack, 0);
    if (temp_condition)
    {
        free_ast(temp_condition); // Free the temporary condition
    }

    if (*i < num_tokens && strcmp(tokens[*i].value.str_val, ")") == 0)
    {
        (*i)++; // skip ')'
    }

    // Skip past block parsing — pass NULL log so skip doesn't re-log.
    Node *temp_block = parse_block(tokens, i, num_tokens, scope_stack, NULL,
                                   NULL, false);
    if (temp_block)
    {
        free_ast(temp_block); // Free the temporary block
    }

    printf("[DEBUG] While loop completed: %d iterations unrolled, "
           "first iteration kept for AST\n",
           iteration_count);

    return while_node;
}

// FOR LOOP PARSER
// Structure:  for (init; cond; post) { body }
// Each of init/cond/post is optional. Only the first iteration's cond+body
// are preserved on the AST; init and post are simulated for their
// side-effects on the symbol table, matching the existing while/do-while
// simulation model.
Node *parse_for_statement(Token *tokens, size_t *i, size_t num_tokens,
                          ScopeStack *scope_stack, OutputLog *log,
                          bool outer_condition_active)
{
    int start_line = tokens[*i].line;
    int start_col = tokens[*i].col;

    (*i)++; // consume 'for'

    if (*i >= num_tokens || strcmp(tokens[*i].value.str_val, "(") != 0)
    {
        printf("Error: Expected '(' after 'for' at line %d\n",
               tokens[*i].line);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    (*i)++; // consume '('

    // Push a scope for the for-loop so that `int i = 0` in init is
    // scoped to the loop, matching C semantics.
    SymbolTable *for_scope = create_symbol_table();
    if (!for_scope)
    {
        printf("Error: Failed to create for-loop scope\n");
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    push_scope(scope_stack, for_scope);

    // --- Parse init (var decl, assignment, or empty) ---
    Node *init_node = NULL;
    if (strcmp(tokens[*i].value.str_val, ";") == 0)
    {
        (*i)++; // empty init, consume ';'
    }
    else if (tokens[*i].type == KEYWORD &&
             (strcmp(tokens[*i].value.str_val, "int") == 0 ||
              strcmp(tokens[*i].value.str_val, "char") == 0 ||
              strcmp(tokens[*i].value.str_val, "const") == 0))
    {
        // Always declare the var symbol (even if the loop is dead) so the
        // throwaway re-parse can still resolve identifiers in the header.
        Node *last = NULL;
        init_node = parse_variable_declaration(tokens, i, num_tokens,
                                               scope_stack, &last, true);
    }
    else if (tokens[*i].type == IDENTIFIER)
    {
        init_node = parse_assignment_statement(tokens, i, num_tokens,
                                               scope_stack,
                                               outer_condition_active);
    }
    else
    {
        printf("Error: Invalid init clause in 'for' at line %d\n",
               tokens[*i].line);
        pop_scope(scope_stack);
        free_symbol_table(for_scope);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }

    // Remember position of condition so we can re-parse it each iteration.
    size_t cond_start_pos = *i;

    Node *for_node = createNode(NODE_FOR_STATEMENT, "for",
                                start_line, start_col);
    if (!for_node)
    {
        free_ast(init_node);
        pop_scope(scope_stack);
        free_symbol_table(for_scope);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }

    // Init node appears on the AST as the first thing linked off for_node.
    // Chain: for_node->left = init -> cond -> body
    // (If init is NULL, for_node->left = cond directly.)

    Node *first_condition = NULL;
    Node *first_block = NULL;
    bool first_iteration = true;
    bool condition_active = true;
    int iteration_count = 0;
    LoopControl my_lc = {0, 0};

    if (!outer_condition_active)
        goto skip_simulation;

    while (condition_active)
    {
        iteration_count++;
        if (iteration_count > MAX_LOOP_ITERATIONS)
        {
            fprintf(stderr,
                    "Error: for loop exceeded %d iterations at line %d "
                    "(likely infinite loop — runtime loops not supported)\n",
                    MAX_LOOP_ITERATIONS, start_line);
            free_ast(init_node);
            free_ast(for_node);
            pop_scope(scope_stack);
            free_symbol_table(for_scope);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
        size_t temp_i = cond_start_pos;

        // --- Parse condition (empty condition == true, like C) ---
        Node *condition = NULL;
        bool empty_cond = false;
        if (strcmp(tokens[temp_i].value.str_val, ";") == 0)
        {
            empty_cond = true;
            condition_active = true;
            // Synthesize a literal-1 so the AST has something.
            condition = createNode(NODE_LITERAL_INT, NULL,
                                   tokens[temp_i].line, tokens[temp_i].col);
            if (condition)
                condition->value.int_val = 1;
            temp_i++; // consume ';'
        }
        else
        {
            condition = parse_expression(tokens, &temp_i, num_tokens,
                                         scope_stack, 0);
            if (!condition)
            {
                printf("Error: Failed to parse for condition\n");
                free_ast(init_node);
                free_ast(for_node);
                pop_scope(scope_stack);
                free_symbol_table(for_scope);
                free_scope_stack(scope_stack);
                free_tokens(tokens, num_tokens);
                exit(1);
            }

            if (temp_i >= num_tokens ||
                strcmp(tokens[temp_i].value.str_val, ";") != 0)
            {
                int eline = tokens[temp_i < num_tokens ? temp_i : temp_i - 1].line;
                int ecol = tokens[temp_i < num_tokens ? temp_i : temp_i - 1].col;
                error_at(eline, ecol,
                         "expected ';' after for condition");
                // unreachable — kept for ownership clarity:
                free_ast(condition);
                free_ast(init_node);
                free_ast(for_node);
                pop_scope(scope_stack);
                free_symbol_table(for_scope);
                free_scope_stack(scope_stack);
                free_tokens(tokens, num_tokens);
                exit(1);
            }
            temp_i++; // consume ';'

            // Evaluate condition against current symbol state
            if (condition->type == NODE_LITERAL_INT)
            {
                condition_active = condition->value.int_val != 0;
            }
            else if (condition->type == NODE_IDENTIFIER)
            {
                Symbol *sym = find_symbol_in_scope_stack(
                    scope_stack, condition->value.str_val);
                condition_active = sym ? (sym->value != 0) : false;
            }
        }

        // If condition is false, stop before parsing body/post.
        if (!condition_active)
        {
            if (!first_iteration || empty_cond)
            {
                // We already have a first_condition saved, or condition is
                // synthetic; this one is disposable.
                free_ast(condition);
            }
            else
            {
                // Save the final (failing) condition on the AST.
                first_condition = condition;
            }
            break;
        }

        // Remember where the post clause starts so we can skip past it.
        size_t post_start_pos = temp_i;

        // Skip past post clause to find the body block.
        // Post clause runs until the matching ')'. We don't parse it yet —
        // the body must run first, then the post.
        int paren_depth = 1;
        while (temp_i < num_tokens && paren_depth > 0)
        {
            if (tokens[temp_i].type == SEPARATOR)
            {
                const char *s = tokens[temp_i].value.str_val;
                if (s && strcmp(s, "(") == 0)
                    paren_depth++;
                else if (s && strcmp(s, ")") == 0)
                    paren_depth--;
            }
            if (paren_depth == 0)
                break;
            temp_i++;
        }
        if (temp_i >= num_tokens)
        {
            printf("Error: Unterminated 'for' header at line %d\n",
                   tokens[cond_start_pos].line);
            free_ast(condition);
            free_ast(init_node);
            free_ast(for_node);
            pop_scope(scope_stack);
            free_symbol_table(for_scope);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }
        temp_i++; // consume ')'

        // --- Parse body block ---
        // Reset continue flag per-iteration (break stays set until handled).
        my_lc.continuing = 0;
        Node *block = parse_block(tokens, &temp_i, num_tokens,
                                  scope_stack, log, &my_lc, true);
        if (!block)
        {
            free_ast(condition);
            free_ast(init_node);
            free_ast(for_node);
            pop_scope(scope_stack);
            free_symbol_table(for_scope);
            free_scope_stack(scope_stack);
            free_tokens(tokens, num_tokens);
            exit(1);
        }

        // If body executed `break;`, skip the post clause and exit loop.
        if (my_lc.breaking)
        {
            my_lc.breaking = 0;
            if (first_iteration)
            {
                first_condition = condition;
                first_block = block;
                first_iteration = false;
            }
            else
            {
                free_ast(condition);
                free_ast(block);
            }
            break;
        }

        // --- Execute post (for symbol-table side effects) ---
        if (post_start_pos < temp_i)
        {
            size_t post_i = post_start_pos;
            // Post can be an assignment (identifier op expr) or empty.
            if (strcmp(tokens[post_i].value.str_val, ")") != 0)
            {
                if (tokens[post_i].type == IDENTIFIER &&
                    post_i + 1 < num_tokens &&
                    tokens[post_i + 1].type == OPERATOR)
                {
                    // Synthesize a trailing ';' context by using a scratch
                    // parse: parse_assignment_statement expects ';' to
                    // terminate. The tokens have ')' instead. Instead of
                    // synthesizing, manually evaluate: identifier op expr.
                    // We'll reuse parse_assignment_statement by temporarily
                    // swapping the ')' for a ';' — but we can't mutate
                    // tokens safely. Simplest: inline-evaluate.
                    Token id_tok = tokens[post_i];
                    Token op_tok = tokens[post_i + 1];
                    size_t e_i = post_i + 2;
                    Node *rhs = parse_expression(tokens, &e_i, num_tokens,
                                                 scope_stack, 0);
                    if (rhs)
                    {
                        SymbolTable *target = NULL;
                        for (int j = scope_stack->size - 1; j >= 0; j--)
                        {
                            if (find_symbol(scope_stack->tables[j],
                                            id_tok.value.str_val))
                            {
                                target = scope_stack->tables[j];
                                break;
                            }
                        }
                        if (target)
                        {
                            Symbol *sym = find_symbol(target,
                                                      id_tok.value.str_val);
                            int cur = sym ? sym->value : 0;
                            int rhs_val = 0;
                            if (rhs->type == NODE_LITERAL_INT)
                                rhs_val = rhs->value.int_val;
                            else if (rhs->type == NODE_IDENTIFIER)
                            {
                                Symbol *rs = find_symbol_in_scope_stack(
                                    scope_stack, rhs->value.str_val);
                                if (rs)
                                    rhs_val = rs->value;
                            }
                            const char *op = op_tok.value.str_val;
                            int res = cur;
                            if (strcmp(op, "=") == 0)
                                res = rhs_val;
                            else if (strcmp(op, "+=") == 0)
                                res = cur + rhs_val;
                            else if (strcmp(op, "-=") == 0)
                                res = cur - rhs_val;
                            else if (strcmp(op, "*=") == 0)
                                res = cur * rhs_val;
                            else if (strcmp(op, "/=") == 0 && rhs_val != 0)
                                res = cur / rhs_val;
                            else if (strcmp(op, "%=") == 0 && rhs_val != 0)
                                res = cur % rhs_val;
                            else if (strcmp(op, "<<=") == 0)
                                res = cur << rhs_val;
                            else if (strcmp(op, ">>=") == 0)
                                res = cur >> rhs_val;
                            update_symbol(target,
                                          id_tok.value.str_val, res);
                        }
                        free_ast(rhs);
                    }
                }
            }
        }

        if (first_iteration)
        {
            first_condition = condition;
            first_block = block;
            first_iteration = false;
        }
        else
        {
            free_ast(condition);
            free_ast(block);
        }
    }

    // Link AST: for_node->left = init (if any) -> cond -> block
    if (init_node)
    {
        for_node->left = init_node;
        // Walk to end of init's sibling chain (var decls can be chained
        // via ','). Attach first_condition at the end.
        Node *tail = init_node;
        while (tail->right)
            tail = tail->right;
        tail->right = first_condition;
    }
    else
    {
        for_node->left = first_condition;
    }
    if (first_condition)
        first_condition->right = first_block;

    goto after_link;

skip_simulation:
    // Dead-branch for-loop: nothing useful to put on the AST. init_node
    // was built for scoping (to declare loop-header vars so header
    // re-parse finds them) but is orphaned; free it to avoid a leak.
    if (init_node)
    {
        free_ast(init_node);
        init_node = NULL;
    }

after_link:
    // --- Advance main parser index past the entire for statement ---
    *i = cond_start_pos;

    // Skip condition
    if (strcmp(tokens[*i].value.str_val, ";") == 0)
    {
        (*i)++;
    }
    else
    {
        Node *skip_cond = parse_expression(tokens, i, num_tokens,
                                           scope_stack, 0);
        if (skip_cond)
            free_ast(skip_cond);
        if (*i < num_tokens && strcmp(tokens[*i].value.str_val, ";") == 0)
            (*i)++;
    }

    // Skip post (to matching ')')
    int depth = 1;
    while (*i < num_tokens && depth > 0)
    {
        if (tokens[*i].type == SEPARATOR)
        {
            const char *s = tokens[*i].value.str_val;
            if (s && strcmp(s, "(") == 0)
                depth++;
            else if (s && strcmp(s, ")") == 0)
            {
                depth--;
                if (depth == 0)
                    break;
            }
        }
        (*i)++;
    }
    if (*i < num_tokens && strcmp(tokens[*i].value.str_val, ")") == 0)
        (*i)++; // consume ')'

    // Skip body block — NULL log so re-parse doesn't re-log.
    Node *skip_block = parse_block(tokens, i, num_tokens, scope_stack, NULL,
                                   NULL, false);
    if (skip_block)
        free_ast(skip_block);

    // Pop the for-scope
    pop_scope(scope_stack);
    free_symbol_table(for_scope);

    printf("[DEBUG] For loop completed: %d iterations unrolled\n",
           iteration_count);

    return for_node;
}

static Node *parse_statement(Token *tokens, size_t *i, size_t num_tokens,
                             ScopeStack *scope_stack, OutputLog *log,
                             LoopControl *lc,
                             Node **last_node_out, bool condition_active)
{
    if (*i >= num_tokens)
    {
        return NULL;
    }

    Token token = tokens[*i];
    Node *stmt = NULL;
    Node *last_node = NULL;

    if (token.type == KEYWORD &&
        (strcmp(token.value.str_val, "int") == 0 ||
         strcmp(token.value.str_val, "char") == 0 ||
         strcmp(token.value.str_val, "const") == 0))
    {
        stmt = parse_variable_declaration(tokens, i, num_tokens, scope_stack,
                                          &last_node, condition_active);
    }
    else if (token.type == KEYWORD && strcmp(token.value.str_val, "exit") == 0)
    {
        stmt = parse_exit_statement(tokens, i, num_tokens, scope_stack,
                                    log, condition_active);
        last_node = stmt;
    }
    else if (token.type == KEYWORD &&
             strcmp(token.value.str_val, "printf") == 0)
    {
        stmt = parse_printf_statement(tokens, i, num_tokens, scope_stack,
                                      log, condition_active);
        last_node = stmt;
    }
    else if (token.type == KEYWORD &&
             (strcmp(token.value.str_val, "break") == 0 ||
              strcmp(token.value.str_val, "continue") == 0))
    {
        bool is_break = strcmp(token.value.str_val, "break") == 0;
        int kw_line = token.line;
        int kw_col = token.col;
        (*i)++;
        if (*i >= num_tokens || tokens[*i].type != SEPARATOR ||
            strcmp(tokens[*i].value.str_val, ";") != 0)
        {
            error_at(kw_line, kw_col, "expected ';' after '%s'",
                     is_break ? "break" : "continue");
        }
        (*i)++;
        // Only enforce "outside of loop" when this code is actually live.
        // The throwaway re-parse pass in loop simulators runs with
        // condition_active=false and lc=NULL; it shouldn't error.
        if (condition_active)
        {
            if (!lc)
            {
                error_at(kw_line, kw_col,
                         "'%s' used outside of loop",
                         is_break ? "break" : "continue");
            }
            if (is_break)
                lc->breaking = 1;
            else
                lc->continuing = 1;
        }
        stmt = createNode(is_break ? NODE_BREAK : NODE_CONTINUE,
                          is_break ? "break" : "continue",
                          kw_line, kw_col);
        last_node = stmt;
    }
    else if (token.type == KEYWORD &&
             ((strcmp(token.value.str_val, "if") == 0) ||
              (strcmp(token.value.str_val, "else") == 0)))
    {
        stmt = parse_else_statement(tokens, i, num_tokens, scope_stack, log,
                                    lc, &last_node, condition_active);
    }
    else if (token.type == KEYWORD &&
             strcmp(token.value.str_val, "while") == 0)
    {
        stmt = parse_while_statement(tokens, i, num_tokens, scope_stack, log,
                                     condition_active);
        last_node = stmt;
    }
    else if (token.type == KEYWORD && strcmp(token.value.str_val, "do") == 0)
    {
        stmt = parse_do_while_statement(tokens, i, num_tokens, scope_stack,
                                        log, condition_active);
        last_node = stmt;
    }
    else if (token.type == KEYWORD && strcmp(token.value.str_val, "for") == 0)
    {
        stmt = parse_for_statement(tokens, i, num_tokens, scope_stack, log,
                                   condition_active);
        last_node = stmt;
    }
    else if (token.type == SEPARATOR && strcmp(token.value.str_val, "{") == 0)
    {
        stmt = parse_block(tokens, i, num_tokens, scope_stack, log,
                           lc, condition_active);
        last_node = stmt;
    }
    else if (token.type == IDENTIFIER &&
             *i + 1 < num_tokens &&
             tokens[*i + 1].type == OPERATOR &&
             (strcmp(tokens[*i + 1].value.str_val, "=") == 0 ||
              strcmp(tokens[*i + 1].value.str_val, "+=") == 0 ||
              strcmp(tokens[*i + 1].value.str_val, "-=") == 0 ||
              strcmp(tokens[*i + 1].value.str_val, "*=") == 0 ||
              strcmp(tokens[*i + 1].value.str_val, "/=") == 0 ||
              strcmp(tokens[*i + 1].value.str_val, "%=") == 0 ||
              strcmp(tokens[*i + 1].value.str_val, "<<=") == 0 ||
              strcmp(tokens[*i + 1].value.str_val, ">>=") == 0))
    {
        stmt = parse_assignment_statement(tokens, i, num_tokens, scope_stack,
                                          condition_active);
        last_node = stmt;
    }
    else
    {
        printf("Error: Unsupported statement at line %d\n", token.line);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }

    if (last_node_out)
    {
        *last_node_out = last_node;
    }
    return stmt;
}

static Node *parse_block(Token *tokens, size_t *i, size_t num_tokens,
                         ScopeStack *scope_stack, OutputLog *log,
                         LoopControl *lc,
                         bool condition_active)
{
    if (*i >= num_tokens)
    {
        printf("Error: Unexpected end of input, expected '{'\n");
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }

    Token token = tokens[*i];
    if (token.type != SEPARATOR || strcmp(token.value.str_val, "{") != 0)
    {
        printf("Error: Expected '{' at line %d\n", token.line);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    (*i)++;

    // Create new scope
    SymbolTable *block_scope = create_symbol_table();
    if (!block_scope)
    {
        printf("Error: Failed to create symbol table\n");
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    push_scope(scope_stack, block_scope);

    Node *block_node = createNode(NODE_BLOCK, "{", token.line, token.col);
    if (!block_node)
    {
        pop_scope(scope_stack);
        free_symbol_table(block_scope);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    Node *current_stmt = NULL;

    while (*i < num_tokens)
    {
        token = tokens[*i];

        // Check for end of block
        if (token.type == SEPARATOR && strcmp(token.value.str_val, "}") == 0)
        {
            (*i)++;
            pop_scope(scope_stack);
            free_symbol_table(block_scope);
            return block_node;
        }

        Node *last_node = NULL;
        // If break/continue was triggered in this iteration, skip the
        // side-effects of remaining statements in this block. We don't
        // gate on log->exit_emitted here because assignments need to keep
        // running so loop counters terminate — the log helpers already
        // ignore appends once exit is emitted.
        bool effective_active = condition_active;
        if (lc && (lc->breaking || lc->continuing))
            effective_active = false;
        Node *stmt = parse_statement(tokens, i, num_tokens, scope_stack,
                                     log, lc, &last_node, effective_active);

        if (stmt)
        {
            if (!block_node->left)
            {
                block_node->left = stmt;
                current_stmt = last_node ? last_node : stmt;
            }
            else
            {
                current_stmt->right = stmt;
                current_stmt = last_node ? last_node : stmt;
            }
        }
    }

    printf("Error: Unexpected end of input before closing '}'\n");
    free_ast(block_node);
    pop_scope(scope_stack);
    free_symbol_table(block_scope);
    free_scope_stack(scope_stack);
    free_tokens(tokens, num_tokens);
    exit(1);
}

// Main Parser
Node *parse(Token *tokens, size_t num_tokens, OutputLog *log)
{
    if (num_tokens == 0)
        return NULL;

    ScopeStack *scope_stack = create_scope_stack();
    if (!scope_stack)
    {
        printf("Error: Failed to create scope stack\n");
        return NULL;
    }

    SymbolTable *global_scope = create_symbol_table();
    if (!global_scope)
    {
        printf("Error: Failed to create global scope\n");
        free_scope_stack(scope_stack);
        return NULL;
    }
    push_scope(scope_stack, global_scope); // Global scope

    Node *root = createNode(NODE_BEGIN, "program", 0, 0);
    if (!root)
    {
        printf("Error: Failed to create root node\n");
        free_scope_stack(scope_stack);
        return NULL;
    }
    Node *current = NULL;

    for (size_t i = 0; i < num_tokens;)
    {
        Node *last_node = NULL;

        // Default condition is true; no enclosing loop at top level.
        Node *stmt = parse_statement(tokens, &i, num_tokens, scope_stack,
                                     log, NULL, &last_node, true);
        if (!stmt)
        {
            // If statement parsing failed, clean up and exit
            free_ast(root);
            free_scope_stack(scope_stack);
            return NULL;
        }

        if (!current)
        {
            root->left = stmt;
        }
        else
        {
            current->right = stmt;
        }
        current = last_node ? last_node : stmt;
        printf(
            "DEBUG LINK: linked stmt=%p, current now = %p\n",
            (void *)stmt, (void *)current);
    }

    free_scope_stack(scope_stack);
    return root;
}