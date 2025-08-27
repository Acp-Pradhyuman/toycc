#include <assert.h>
#include <string.h>
#include "codegen.h"

void traverse_tree(Node *node, FILE *file, ScopeType scope, bool *exit_emitted,
                   bool active_block)
{
    if (node == NULL || *exit_emitted)
        return;

    switch (node->type)
    {
    case NODE_EXIT_CALL:
        if (!*exit_emitted && active_block)
        {
            fprintf(file, "\tmov rax, 60\n");
            if (node->left)
                fprintf(file, "\tmov rdi, %d\n", node->left->value.int_val);
            else
                fprintf(file, "\tmov rdi, 0\n");
            fprintf(file, "\tsyscall\n");
            *exit_emitted = true;
        }
        return;

    case NODE_IF_STATEMENT:
    {
        Node *cond = node->left;
        Node *then_block = cond ? cond->right : NULL;
        bool condition_active = false;

        // Evaluate condition if possible
        if (cond && cond->type == NODE_LITERAL_INT)
        {
            condition_active = (cond->value.int_val != 0);
        }

        // Process then block if condition is true and we're in an active block
        if (then_block && active_block && condition_active)
        {
            traverse_tree(then_block, file, scope, exit_emitted, true);
        }

        // Only process else/else if if the if condition was false and
        // we're active
        if (active_block && !condition_active)
        {
            Node *else_node = node->right;
            bool found_active_else = false;

            while (else_node && !found_active_else && !*exit_emitted)
            {
                switch (else_node->type)
                {
                case NODE_ELSE_IF_STATEMENT:
                {
                    Node *else_if_cond = else_node->left;
                    Node *else_if_block =
                        else_if_cond ? else_if_cond->right : NULL;
                    bool else_if_active = false;

                    if (else_if_cond && else_if_cond->type == NODE_LITERAL_INT)
                    {
                        else_if_active = (else_if_cond->value.int_val != 0);
                    }
                    else if (else_if_cond)
                    {
                        // For non-constant conditions, assume true
                        else_if_active = true;
                    }

                    if (else_if_block && else_if_active)
                    {
                        traverse_tree(else_if_block, file, scope,
                                      exit_emitted, true);
                        // Found active else-if, skip rest
                        found_active_else = true;
                    }
                    break;
                }

                case NODE_ELSE_STATEMENT:
                    // Else block is always active if we get here
                    traverse_tree(else_node->left, file, scope, 
                        exit_emitted, true);
                        // Found else, skip any remaining else-ifs
                    found_active_else = true; 
                    break;

                default:
                    break;
                }

                else_node = else_node->right;
            }
        }

        // Continue with siblings
        traverse_tree(node->right, file, scope, exit_emitted, active_block);
        return;
    }

    case NODE_WHILE_STATEMENT:
    {
        Node *cond = node->left;
        Node *loop_block = cond ? cond->right : NULL;
        bool condition_active = false;

        if (cond && cond->type == NODE_LITERAL_INT)
        {
            condition_active = cond->value.int_val != 0;
        }
        // else if (cond && cond->type == NODE_IDENTIFIER)
        // {
        //     // or look up variable value if you track it
        //     condition_active = true; 
        // }
        else if (cond)
        {
            condition_active = true; // assume true for non-literals
        }

        if (loop_block && active_block && condition_active)
        {
            traverse_tree(loop_block, file, scope, exit_emitted, true);
        }

        traverse_tree(node->right, file, scope, exit_emitted, active_block);
        return;
    }

    default:
        break;
    }

    traverse_tree(node->left, file, scope, exit_emitted, active_block);
    traverse_tree(node->right, file, scope, exit_emitted, active_block);
}

int generate_code(Node *root, const char *filename)
{
    FILE *file = fopen(filename, "w");
    assert(file && "Failed to open output file");

    fprintf(file, "section .text\n");
    fprintf(file, "global _start\n");
    fprintf(file, "_start:\n");

    bool exit_emitted = false;
    traverse_tree(root, file, SCOPE_GLOBAL, &exit_emitted, true);

    if (!exit_emitted)
    {
        // If no exit() present, emit default
        fprintf(file, "\tmov rax, 60\n");
        fprintf(file, "\tmov rdi, 0\n");
        fprintf(file, "\tsyscall\n");
    }

    fclose(file);
    return 0;
}