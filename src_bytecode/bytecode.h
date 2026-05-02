#ifndef LEDA_BYTECODE_H
#define LEDA_BYTECODE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define BC_MAGIC "LBC0"
#define BC_VERSION 1u

enum bc_opcode {
    BC_OP_HALT = 0,
    BC_OP_CONST = 1,
    BC_OP_POP = 2,
    BC_OP_DUP = 3,
    BC_OP_RETURN = 4,
    BC_OP_JUMP = 5,
    BC_OP_JUMP_IF_FALSE = 6,
    BC_OP_CALL = 7,
    BC_OP_CALL_PRIMITIVE = 8,
    BC_OP_LOAD_LOCAL = 9,
    BC_OP_STORE_LOCAL = 10,
    BC_OP_LOAD_ARG = 11,
    BC_OP_STORE_ARG = 12,
};

enum bc_constant_kind {
    BC_CONST_INTEGER = 1,
    BC_CONST_REAL = 2,
    BC_CONST_STRING = 3,
};

struct bc_buffer {
    uint8_t *data;
    size_t size;
    size_t capacity;
};

struct bc_constant {
    enum bc_constant_kind kind;
    union {
        int64_t integer;
        double real;
        char *string;
    } value;
};

struct bc_function {
    char *name;
    uint32_t arity;
    uint32_t local_count;
    uint32_t max_stack;
    uint32_t flags;
    struct bc_buffer code;
};

struct bc_module {
    uint32_t version;
    uint32_t entry_function;
    struct bc_constant *constants;
    size_t constant_count;
    size_t constant_capacity;
    struct bc_function *functions;
    size_t function_count;
    size_t function_capacity;
};

void bc_module_init(struct bc_module *module);
void bc_module_free(struct bc_module *module);

size_t bc_add_integer_constant(struct bc_module *module, int64_t value);
size_t bc_add_real_constant(struct bc_module *module, double value);
size_t bc_add_string_constant(struct bc_module *module, const char *value);
size_t bc_add_function(struct bc_module *module, const char *name, uint32_t arity, uint32_t local_count, uint32_t max_stack,
                       uint32_t flags);

int bc_emit_opcode(struct bc_function *function, enum bc_opcode opcode);
int bc_emit_u8(struct bc_function *function, uint8_t value);
int bc_emit_uleb128(struct bc_function *function, uint64_t value);
int bc_emit_sleb128(struct bc_function *function, int64_t value);

int bc_module_write(FILE *output, const struct bc_module *module);
int bc_module_read(FILE *input, struct bc_module *module);

#endif