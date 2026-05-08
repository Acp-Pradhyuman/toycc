#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "error.h"

static const char *g_filename = "<unknown>";
static const char *g_source = NULL;

void error_set_source(const char *filename, const char *text)
{
    if (filename)
        g_filename = filename;
    g_source = text;
}

// Return a pointer to the start of the requested 1-based line in g_source,
// or NULL if unavailable. Writes the line length (excluding newline) to
// *out_len.
static const char *find_line(int line_1based, size_t *out_len)
{
    if (!g_source || line_1based < 1)
        return NULL;
    const char *p = g_source;
    int line = 1;
    while (*p && line < line_1based)
    {
        if (*p == '\n')
            line++;
        p++;
    }
    if (line != line_1based)
        return NULL;
    const char *start = p;
    size_t n = 0;
    while (start[n] && start[n] != '\n')
        n++;
    *out_len = n;
    return start;
}

void error_at(int line, int col, const char *fmt, ...)
{
    fprintf(stderr, "%s:%d:%d: error: ", g_filename, line, col);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);

    size_t line_len = 0;
    const char *line_start = find_line(line, &line_len);
    if (line_start)
    {
        fprintf(stderr, "    ");
        fwrite(line_start, 1, line_len, stderr);
        fputc('\n', stderr);

        // Caret. col is 1-based. Print 4 spaces of indent, then col-1
        // spaces (preserving tab characters from the source).
        fprintf(stderr, "    ");
        for (int c = 0; c < col - 1 && (size_t)c < line_len; c++)
        {
            fputc(line_start[c] == '\t' ? '\t' : ' ', stderr);
        }
        fputs("^\n", stderr);
    }

    exit(1);
}