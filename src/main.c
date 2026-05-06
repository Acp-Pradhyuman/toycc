#include <stdio.h>
#include <stdlib.h>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "codegen/codegen.h"
#include "output/output_log.h"
#include "error/error.h"

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <source_file>\n", argv[0]);
        return 1;
    }

    FILE *file = fopen(argv[1], "r");
    if (!file)
    {
        perror("Failed to open file");
        return 1;
    }

    // Slurp entire source into a buffer so error_at() can show context.
    // We also rewind so the lexer still sees the full content.
    fseek(file, 0, SEEK_END);
    long fsize = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *source_text = NULL;
    if (fsize >= 0)
    {
        source_text = malloc((size_t)fsize + 1);
        if (source_text)
        {
            size_t got = fread(source_text, 1, (size_t)fsize, file);
            source_text[got] = '\0';
            rewind(file);
            error_set_source(argv[1], source_text);
        }
    }

    size_t num_tokens = 0;
    Token *tokens = lexer(file, &num_tokens);

    // // Cleanup heap-allocated strings
    // for (size_t i = 0; i < num_tokens; ++i)
    // {
    //     if (tokens[i].type != INT)
    //     {
    //         free(tokens[i].value.str_val);

    //         // to avoid dangling pointer
    //         tokens[i].value.str_val = NULL;
    //     }
    // }

    // Print all tokens collected
    printf("\n--- All Tokens ---\n");
    for (size_t i = 0; i < num_tokens; ++i)
    {
        print_token(tokens[i]);
    }

    OutputLog *log = output_log_create();
    Node *root = parse(tokens, num_tokens, log);

    printf("\n--- Syntax Tree ---\n");
    // left child right sibling
    treeTraversal(root, 0);

    // Default output name if not provided
    char *output_name = (argc >= 3) ? argv[2] : "generated";

    // Code Generation — driven by the output log, not the AST.
    char asm_filename[256];
    snprintf(asm_filename, sizeof(asm_filename), "%s.asm", output_name);
    generate_code(log, asm_filename);

    // Compilation Pipeline
    char nasm_cmd[256];
    char ld_cmd[256];

    // Assemble with NASM
    snprintf(nasm_cmd, sizeof(nasm_cmd), "nasm -f elf64 %s.asm -o %s.o",
             output_name, output_name);
    if (system(nasm_cmd) != 0)
    {
        fprintf(stderr, "NASM assembly failed\n");
        goto cleanup;
    }

    // Link with ld
    snprintf(ld_cmd, sizeof(ld_cmd), "ld %s.o -o %s",
             output_name, output_name);
    if (system(ld_cmd) != 0)
    {
        fprintf(stderr, "Linking failed\n");
        goto cleanup;
    }

    printf("Compilation successful. Output: %s\n", output_name);

cleanup:
    // Resource cleanup
    free_ast(root);
    output_log_free(log);
    printf("\nExiting\n");
    free_tokens(tokens, num_tokens);
    fclose(file);
    free(source_text);
    return 0;
}