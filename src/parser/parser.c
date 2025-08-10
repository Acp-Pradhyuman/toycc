#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "parser.h"

// Forward declarations
static Node *parse_expression(Token *tokens, size_t *i, size_t num_tokens,
                              ScopeStack *scope_stack, int min_precedence);
static Node *parse_primary(Token *tokens, size_t *i, size_t num_tokens,
                           ScopeStack *scope_stack);
static Node *parse_block(Token *tokens, size_t *i, size_t num_tokens,
                         ScopeStack *scope_stack, bool condition_active);

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
        else if (strcmp(token.value.str_val, "int") == 0)
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
                printf("Error: Undefined variable '%s' at line %d\n",
                       token.value.str_val, token.line);
                free_ast(node);
                free_scope_stack(scope_stack);
                free_tokens(tokens, num_tokens);
                exit(1);
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

    return left;
}

// Variable Declaration Parser
static Node *parse_variable_declaration(Token *tokens, size_t *i,
                                        size_t num_tokens,
                                        ScopeStack *scope_stack,
                                        Node **last_decl_out,
                                        bool condition_active)
{
    if (*i >= num_tokens || tokens[*i].type != KEYWORD ||
        strcmp(tokens[*i].value.str_val, "int") != 0)
    {
        printf("Error: Expected 'int' keyword at line %d\n", tokens[*i].line);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
    }
    (*i)++;

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

        if (*i < num_tokens && strcmp(tokens[*i].value.str_val, "=") == 0)
        {
            (*i)++;
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

        // Only add symbol if the condition is active
        if (condition_active)
        {
            add_symbol(current_table, id_token.value.str_val, VAR_INT,
                       initial_value, id_token.line, id_token.col);
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
        printf("Error: Undefined variable '%s' at line %d\n",
               id_token.value.str_val, id_token.line);
        free_scope_stack(scope_stack);
        free_tokens(tokens, num_tokens);
        exit(1);
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

    assign_node->right = value_to_store;

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
                assign_node->right = literal_result;
            }
        }
    }

    return assign_node;
}

// Exit Statement Parser
static Node *parse_exit_statement(Token *tokens, size_t *i, size_t num_tokens,
                                  ScopeStack *scope_stack)
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
    return exit_node;
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
                                ScopeStack *scope_stack, Node **last_node_out)
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

    // Parse then block with the condition status
    Node *then_block = parse_block(tokens, i, num_tokens, scope_stack,
                                   condition_active);
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
                                      ScopeStack *scope_stack, Node *if_node,
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

        // Parse then block
        Node *else_if_block = parse_block(tokens, i, num_tokens,
                                          scope_stack, condition_active);
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
                                  ScopeStack *scope_stack, Node **last_node_out)
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
                                       scope_stack, NULL);
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
    parse_else_if_statements(tokens, i, num_tokens, scope_stack,
                             if_node, any_condition_active, &last_else_if);

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

        // Parse else block
        Node *else_block = parse_block(tokens, i, num_tokens, scope_stack,
                                       else_active);
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
                               ScopeStack *scope_stack)
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

    do
    {
        iteration_count++;
        size_t temp_i = block_start_pos;

        printf("[DEBUG] Do-While Iteration %d: Parsing block "
               "at token index %zu\n",
               iteration_count, temp_i);

        // Parse block first (this is the key difference from while loop)
        Node *block = parse_block(tokens, &temp_i, num_tokens,
                                  scope_stack, condition_active);
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

    } while (condition_active);

    // Link ONLY the first iteration nodes to the do_while_node AST
    // Structure: do_while_node->left = first_block,
    // first_block->right = first_condition
    do_while_node->left = first_block;
    if (first_block)
        first_block->right = first_condition;

    // Advance parser index past the entire do-while statement
    *i = block_start_pos;

    // Skip past block parsing
    Node *temp_block = parse_block(tokens, i, num_tokens, scope_stack, false);
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
                            ScopeStack *scope_stack)
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

    while (condition_active)
    {
        iteration_count++;
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

        // Parse block - this updates symbol table for semantic analysis
        Node *block = parse_block(tokens, &temp_i, num_tokens, scope_stack,
                                  condition_active);
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
    }

    // Link ONLY the first iteration nodes to the while_node AST
    while_node->left = first_condition;
    if (first_condition)
        first_condition->right = first_block;

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

    // Skip past block parsing
    Node *temp_block = parse_block(tokens, i, num_tokens, scope_stack, false);
    if (temp_block)
    {
        free_ast(temp_block); // Free the temporary block
    }

    printf("[DEBUG] While loop completed: %d iterations unrolled, "
           "first iteration kept for AST\n",
           iteration_count);

    return while_node;
}

static Node *parse_statement(Token *tokens, size_t *i, size_t num_tokens,
                             ScopeStack *scope_stack, Node **last_node_out,
                             bool condition_active)
{
    if (*i >= num_tokens)
    {
        return NULL;
    }

    Token token = tokens[*i];
    Node *stmt = NULL;
    Node *last_node = NULL;

    if (token.type == KEYWORD && strcmp(token.value.str_val, "int") == 0)
    {
        stmt = parse_variable_declaration(tokens, i, num_tokens, scope_stack,
                                          &last_node, condition_active);
    }
    else if (token.type == KEYWORD && strcmp(token.value.str_val, "exit") == 0)
    {
        stmt = parse_exit_statement(tokens, i, num_tokens, scope_stack);
        last_node = stmt;
    }
    else if (token.type == KEYWORD &&
             ((strcmp(token.value.str_val, "if") == 0) ||
              (strcmp(token.value.str_val, "else") == 0)))
    {
        stmt = parse_else_statement(tokens, i, num_tokens, scope_stack,
                                    &last_node);
    }
    else if (token.type == KEYWORD &&
             strcmp(token.value.str_val, "while") == 0)
    {
        stmt = parse_while_statement(tokens, i, num_tokens, scope_stack);
        last_node = stmt;
    }
    else if (token.type == KEYWORD && strcmp(token.value.str_val, "do") == 0)
    {
        stmt = parse_do_while_statement(tokens, i, num_tokens, scope_stack);
        last_node = stmt;
    }
    else if (token.type == SEPARATOR && strcmp(token.value.str_val, "{") == 0)
    {
        stmt = parse_block(tokens, i, num_tokens, scope_stack, condition_active);
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
                         ScopeStack *scope_stack, bool condition_active)
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
        Node *stmt = parse_statement(tokens, i, num_tokens, scope_stack,
                                     &last_node, condition_active);

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
Node *parse(Token *tokens, size_t num_tokens)
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

        // Default condition is true
        Node *stmt = parse_statement(tokens, &i, num_tokens, scope_stack,
                                     &last_node, true);
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