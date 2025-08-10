#ifndef CODEGEN_H
// "If CODEGEN_H is not defined yet..."
#define CODEGEN_H
// "...define it now."

#include <stdio.h>
#include "../parser/parser.h"

typedef enum {
    SCOPE_GLOBAL,
    SCOPE_CONTROL_FLOW  // Inside if/while/etc.
} ScopeType;

// int generate_code(Node *root, char *filename);
int generate_code(Node *root, const char *filename);

#endif // CODEGEN_H