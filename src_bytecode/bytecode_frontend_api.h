#ifndef LEDA_BYTECODE_FRONTEND_API_H
#define LEDA_BYTECODE_FRONTEND_API_H

#include "ast.h"

int bytecode_parse_file(char *input_path, struct symbolTableRecord **out_symbols, struct statementRecord **out_statement);

#endif