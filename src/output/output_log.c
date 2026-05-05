#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "output_log.h"

#define INITIAL_LOG_CAPACITY 32

OutputLog *output_log_create(void)
{
    OutputLog *log = malloc(sizeof(OutputLog));
    if (!log)
        return NULL;
    log->ops = malloc(sizeof(OutputOp) * INITIAL_LOG_CAPACITY);
    if (!log->ops)
    {
        free(log);
        return NULL;
    }
    log->count = 0;
    log->capacity = INITIAL_LOG_CAPACITY;
    log->exit_emitted = 0;
    return log;
}

static void ensure_capacity(OutputLog *log)
{
    if (log->count < log->capacity)
        return;
    size_t new_cap = log->capacity * 2;
    OutputOp *new_ops = realloc(log->ops, sizeof(OutputOp) * new_cap);
    if (!new_ops)
    {
        fprintf(stderr, "OutputLog: realloc failed\n");
        exit(EXIT_FAILURE);
    }
    log->ops = new_ops;
    log->capacity = new_cap;
}

void output_log_append_write(OutputLog *log, const char *bytes, size_t len)
{
    if (!log || log->exit_emitted)
        return;
    ensure_capacity(log);
    char *copy = malloc(len);
    if (!copy)
    {
        fprintf(stderr, "OutputLog: malloc failed\n");
        exit(EXIT_FAILURE);
    }
    memcpy(copy, bytes, len);
    log->ops[log->count].kind = OUTPUT_WRITE;
    log->ops[log->count].bytes = copy;
    log->ops[log->count].len = len;
    log->ops[log->count].exit_code = 0;
    log->count++;
}

void output_log_append_exit(OutputLog *log, int code)
{
    if (!log || log->exit_emitted)
        return;
    ensure_capacity(log);
    log->ops[log->count].kind = OUTPUT_EXIT;
    log->ops[log->count].bytes = NULL;
    log->ops[log->count].len = 0;
    log->ops[log->count].exit_code = code;
    log->count++;
    log->exit_emitted = 1;
}

void output_log_free(OutputLog *log)
{
    if (!log)
        return;
    for (size_t i = 0; i < log->count; i++)
    {
        if (log->ops[i].kind == OUTPUT_WRITE && log->ops[i].bytes)
            free(log->ops[i].bytes);
    }
    free(log->ops);
    free(log);
}