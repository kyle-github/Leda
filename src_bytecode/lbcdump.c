#include "bytecode.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *argv0) { fprintf(stderr, "usage: %s program.lbc\n", argv0); }

static int read_u64_operand(const struct bc_function *function, size_t *ip, uint64_t *value) {
    size_t index;
    uint64_t result = 0;

    if(*ip + 8 > function->code.size) { return 0; }

    for(index = 0; index < 8; ++index) { result |= ((uint64_t)function->code.data[*ip + index]) << (index * 8); }

    *ip += 8;
    *value = result;
    return 1;
}

static int read_i64_operand(const struct bc_function *function, size_t *ip, int64_t *value) {
    uint64_t raw;

    if(!read_u64_operand(function, ip, &raw)) { return 0; }
    memcpy(value, &raw, sizeof(*value));
    return 1;
}

static void print_escaped_string(FILE *output, const char *text) {
    const unsigned char *cursor = (const unsigned char *)text;

    fputc('"', output);
    for(; *cursor != '\0'; ++cursor) {
        switch(*cursor) {
            case '\\': fputs("\\\\", output); break;
            case '"': fputs("\\\"", output); break;
            case '\n': fputs("\\n", output); break;
            case '\r': fputs("\\r", output); break;
            case '\t': fputs("\\t", output); break;

            default:
                if(*cursor < 32 || *cursor > 126) {
                    fprintf(output, "\\x%02x", *cursor);
                } else {
                    fputc((int)*cursor, output);
                }
                break;
        }
    }
    fputc('"', output);
}

static const char *opcode_name(enum bc_opcode opcode) {
    switch(opcode) {
        case BC_OP_HALT: return "HALT";
        case BC_OP_CONST: return "CONST";
        case BC_OP_POP: return "POP";
        case BC_OP_DUP: return "DUP";
        case BC_OP_RETURN: return "RETURN";
        case BC_OP_JUMP: return "JUMP";
        case BC_OP_JUMP_IF_FALSE: return "JUMP_IF_FALSE";
        case BC_OP_CALL: return "CALL";
        case BC_OP_CALL_PRIMITIVE: return "CALL_PRIMITIVE";
        case BC_OP_LOAD_LOCAL: return "LOAD_LOCAL";
        case BC_OP_STORE_LOCAL: return "STORE_LOCAL";
        case BC_OP_LOAD_ARG: return "LOAD_ARG";
        case BC_OP_STORE_ARG: return "STORE_ARG";
        case BC_OP_MAKE_CLOSURE: return "MAKE_CLOSURE";
        case BC_OP_CALL_CLOSURE: return "CALL_CLOSURE";
        case BC_OP_LOAD_CAPTURE_LOCAL: return "LOAD_CAPTURE_LOCAL";
        case BC_OP_STORE_CAPTURE_LOCAL: return "STORE_CAPTURE_LOCAL";
        case BC_OP_LOAD_CAPTURE_ARG: return "LOAD_CAPTURE_ARG";
        case BC_OP_STORE_CAPTURE_ARG: return "STORE_CAPTURE_ARG";
    }

    return "<unknown>";
}

static void print_constant_summary(const struct bc_module *module, uint64_t index) {
    if(index >= module->constant_count) {
        printf(" [invalid constant]");
        return;
    }

    switch(module->constants[index].kind) {
        case BC_CONST_INTEGER:
            printf(" ; const[%