#include <assert.h>
#include <stdbool.h>
#include "codegen.h"

// Emit the .data section by walking the log and writing each OUTPUT_WRITE's
// bytes as a labeled byte sequence. The label `str_<i>` matches the index
// in the log so the .text pass can reference it directly.
static void emit_data_section(FILE *file, OutputLog *log)
{
    bool has_strings = false;
    for (size_t i = 0; i < log->count; i++)
    {
        if (log->ops[i].kind == OUTPUT_WRITE)
        {
            has_strings = true;
            break;
        }
    }
    if (!has_strings)
        return;

    fprintf(file, "section .data\n");
    for (size_t i = 0; i < log->count; i++)
    {
        OutputOp *op = &log->ops[i];
        if (op->kind != OUTPUT_WRITE)
            continue;
        fprintf(file, "str_%zu: db ", i);
        for (size_t b = 0; b < op->len; b++)
        {
            fprintf(file, "%s%d",
                    b ? "," : "",
                    (unsigned char)op->bytes[b]);
        }
        fprintf(file, "\n");
    }
}

static void emit_text_section(FILE *file, OutputLog *log)
{
    fprintf(file, "section .text\n");
    fprintf(file, "global _start\n");
    fprintf(file, "_start:\n");

    bool exit_emitted = false;
    for (size_t i = 0; i < log->count; i++)
    {
        OutputOp *op = &log->ops[i];
        if (op->kind == OUTPUT_WRITE)
        {
            fprintf(file, "\tmov rax, 1\n");
            fprintf(file, "\tmov rdi, 1\n");
            fprintf(file, "\tlea rsi, [rel str_%zu]\n", i);
            fprintf(file, "\tmov rdx, %zu\n", op->len);
            fprintf(file, "\tsyscall\n");
        }
        else if (op->kind == OUTPUT_EXIT)
        {
            fprintf(file, "\tmov rax, 60\n");
            fprintf(file, "\tmov rdi, %d\n", op->exit_code);
            fprintf(file, "\tsyscall\n");
            exit_emitted = true;
            break; // Anything after a live exit is dead.
        }
    }

    if (!exit_emitted)
    {
        fprintf(file, "\tmov rax, 60\n");
        fprintf(file, "\tmov rdi, 0\n");
        fprintf(file, "\tsyscall\n");
    }
}

int generate_code(OutputLog *log, const char *filename)
{
    FILE *file = fopen(filename, "w");
    assert(file && "Failed to open output file");

    emit_data_section(file, log);
    emit_text_section(file, log);

    fclose(file);
    return 0;
}