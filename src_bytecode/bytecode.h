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
    BC_OP_MAKE_CLOSURE = 13,
    BC_OP_CALL_CLOSURE = 14,
    BC_OP_LOAD_CAPTURE_LOCAL = 15,
    BC_OP_STORE_CAPTURE_LOCAL = 16,
    BC_OP_LOAD_CAPTURE_ARG = 17,
    BC_OP_STORE_CAPTURE_ARG = 18,
    BC_OP_MAKE_REF_LOCAL = 19,
    BC_OP_MAKE_REF_ARG = 20,
    BC_OP_MAKE_REF_CAPTURE_LOCAL = 21,
    BC_OP_MAKE_REF_CAPTURE_ARG = 22,
    BC_OP_LOAD_REF = 23,
    BC_OP_BUILD_INSTANCE = 24,
    BC_OP_LOAD_OBJECT_SLOT = 25,
    BC_OP_STORE_OBJECT_SLOT = 26,
    BC_OP_MAKE_REF_OBJECT_SLOT = 27,
    BC_OP_MAKE_METHOD = 28,
    BC_OP_BR_IF_NOT_KIND = 29,
    BC_OP_REGISTER_BUILTIN = 30,
};

enum bc_constant_kind {
    BC_CONST_INTEGER = 1,
    BC_CONST_REAL = 2,
    BC_CONST_STRING = 3,
    BC_CONST_BOOLEAN = 4,
    BC_CONST_FUNCTION = 5,
    BC_CONST_REFERENCE = 6,
    BC_CONST_OBJECT = 7,
    BC_CONST_ENVREF = 8,
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
        struct {
            uint64_t function_index;
            uint64_t parent_env_index;
        } closure;
        struct bc_constant **slot_ref;
        struct {
            uint64_t slot_count;
            struct bc_constant **slots;
        } object;
        uint64_t env_index;
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
int bc_reserve_function_capacity(struct bc_module *module, size_t needed);
size_t bc_add_function(struct bc_module *module, const char *name, uint32_t arity, uint32_t local_count, uint32_t max_stack,
                       uint32_t flags);

int bc_emit_opcode(struct bc_function *function, enum bc_opcode opcode);
int bc_emit_u8(struct bc_function *function, uint8_t value);
int bc_emit_u64le(struct bc_function *function, uint64_t value);
int bc_emit_i64le(struct bc_function *function, int64_t value);

int bc_module_write(FILE *output, const struct bc_module *module);
int bc_module_read(FILE *input, struct bc_module *module);

#endif