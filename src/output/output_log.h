#ifndef OUTPUT_LOG_H
#define OUTPUT_LOG_H

#include <stddef.h>

typedef enum
{
    OUTPUT_WRITE,
    OUTPUT_EXIT
} OutputKind;

typedef struct
{
    OutputKind kind;
    char *bytes;     // OUTPUT_WRITE: owned buffer of raw bytes
    size_t len;      // OUTPUT_WRITE: byte count
    int exit_code;   // OUTPUT_EXIT: status value
} OutputOp;

typedef struct
{
    OutputOp *ops;
    size_t count;
    size_t capacity;
    // Once an OUTPUT_EXIT has been appended, the simulated program has
    // terminated — any later append_* becomes a no-op. This replaces the
    // `exit_emitted` flag that used to live in codegen.
    int exit_emitted;
} OutputLog;

OutputLog *output_log_create(void);
void output_log_append_write(OutputLog *log, const char *bytes, size_t len);
void output_log_append_exit(OutputLog *log, int code);
void output_log_free(OutputLog *log);

#endif // OUTPUT_LOG_H