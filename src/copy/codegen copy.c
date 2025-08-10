#include <assert.h>
#include <string.h>
#include "codegen.h"

void traverse_tree(Node *node, FILE *file, ScopeType scope,
                   bool *has_top_level_exit)
{
    if (node == NULL || (scope == SCOPE_GLOBAL && *has_top_level_exit))
    {
        return;
    }

    switch (node->type)
    {
    case NODE_EXIT_CALL:
        fprintf(file, "\tmov rax, 60\n");
        if (node->left)
        {
            fprintf(file, "\tmov rdi, %d\n", node->left->value.int_val);
        }
        else
        {
            fprintf(file, "\tmov rdi, 0\n");
        }
        fprintf(file, "\tsyscall\n");

        if (scope == SCOPE_GLOBAL)
        {
            *has_top_level_exit = true;
        }
        return;

    case NODE_IF_STATEMENT:
        // Generate condition evaluation
        fprintf(file, "\t; IF condition\n");
        traverse_tree(node->left, file, SCOPE_CONTROL_FLOW,
                      has_top_level_exit);

        // Generate jump (pseudo-code - need proper label handling)
        fprintf(file, "\tjz .endif\n");

        // Generate then block
        traverse_tree(node->right, file, SCOPE_CONTROL_FLOW,
                      has_top_level_exit);
        fprintf(file, ".endif:\n");
        return;

    case NODE_LITERAL_INT:
        // This will be handled by parent nodes (like exit)
        break;

    // Add cases for other node types
    default:
        break;
    }

    traverse_tree(node->left, file, scope, has_top_level_exit);
    if (!(scope == SCOPE_GLOBAL && *has_top_level_exit))
    {
        traverse_tree(node->right, file, scope, has_top_level_exit);
    }
}

int generate_code(Node *root, const char *filename)
{
    FILE *file = fopen(filename, "w");
    assert(file && "Failed to open output file");

    // Write assembly header
    fprintf(file, "section .text\n");
    fprintf(file, "global _start\n");
    fprintf(file, "_start:\n");

    bool has_top_level_exit = false;
    traverse_tree(root, file, SCOPE_GLOBAL, &has_top_level_exit);

    if (!has_top_level_exit)
    {
        fprintf(file, "\tmov rax, 60\n");
        fprintf(file, "\tmov rdi, 0\n");
        fprintf(file, "\tsyscall\n");
    }

    fclose(file);
    return 0;
}