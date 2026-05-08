#ifndef ERROR_H
#define ERROR_H

// Install the source text so error_at() can show the offending line.
// `text` must remain live for the lifetime of the compilation.
void error_set_source(const char *filename, const char *text);

// Print a GCC-style error with a caret, then exit(1).
//   file:line:col: error: <msg>
//       <offending source line>
//       <spaces>^
// Never returns.
void error_at(int line, int col, const char *fmt, ...)
    __attribute__((noreturn, format(printf, 3, 4)));

#endif // ERROR_H