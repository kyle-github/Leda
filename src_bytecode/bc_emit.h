#ifndef LEDA_BC_EMIT_H
#define LEDA_BC_EMIT_H

#include "ast.h"
#include "bytecode.h"

int bc_compile_top_level(struct symbolTableRecord *symbols, struct statementRecord *first_statement, struct bc_module *module,
                         char *error_buffer, size_t error_buffer_size);

#endif