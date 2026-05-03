#include "bytecode.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void usage(const char *argv0) { fprintf(stderr, "usage: %s program.lbc\n", argv0); }

static int read_u64(const struct bc_function *fn, size_t *ip, uint64_t *value) {
    size_t index;
    uint64_t result = 0;
    if(*ip + 8 > fn->code.size) { return 0; }
    for(index = 0; index < 8; ++index) { result |= ((uint64_t)fn->code.data[*ip + index]) << (index * 8); }
    *ip += 8;
    *value = result;
    return 1;
}

static int read_i64(const struct bc_function *fn, size_t *ip, int64_t *value) {
    uint64_t raw;
    if(!read_u64(fn, ip, &raw)) { return 0; }
    memcpy(value, &raw, sizeof(*value));
    return 1;
}

static void print_escaped(const char *text) {
    const unsigned char *cursor = (const unsigned char *)text;
    putchar('"');
    for(; *cursor != '\0'; ++cursor) {
        switch(*cursor) {
            case '\\': fputs("\\\\", stdout); break;
            case '"': fputs("\\\"", stdout); break;
            case '\n': fputs("\\n", stdout); break;
            case '\r': fputs("\\r", stdout); break;
            case '\t': fputs("\\t", stdout); break;
            default:
                if(*cursor < 32 || *cursor > 126) {
                    printf("\\x%02x", *cursor);
                } else {
                    putchar((int)*cursor);
                }
                break;
        }
    }
    putchar('"');
}

static const char *op_name(enum bc_opcode opcode) {
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
        case BC_OP_MAKE_REF_LOCAL: return "MAKE_REF_LOCAL";
        case BC_OP_MAKE_REF_ARG: return "MAKE_REF_ARG";
        case BC_OP_MAKE_REF_CAPTURE_LOCAL: return "MAKE_REF_CAPTURE_LOCAL";
        case BC_OP_MAKE_REF_CAPTURE_ARG: return "MAKE_REF_CAPTURE_ARG";
        case BC_OP_LOAD_REF: return "LOAD_REF";
        case BC_OP_BUILD_INSTANCE: return "BUILD_INSTANCE";
        case BC_OP_LOAD_OBJECT_SLOT: return "LOAD_OBJECT_SLOT";
        case BC_OP_STORE_OBJECT_SLOT: return "STORE_OBJECT_SLOT";
        case BC_OP_MAKE_REF_OBJECT_SLOT: return "MAKE_REF_OBJECT_SLOT";
        case BC_OP_MAKE_METHOD: return "MAKE_METHOD";
    }
    return "<unknown>";
}

static const char *primitive_name(uint64_t index) {
    static const char *names[] = {"Leda_object_equals",    "Leda_string_compare",   "Leda_string_print",   "Leda_string_concat",
                                  "Leda_integer_equals",   "Leda_integer_plus",     "Leda_integer_minus",  "Leda_integer_times",
                                  "Leda_integer_divide",   "Leda_integer_asString", "Leda_integer_less",   "Leda_integer_or",
                                  "Leda_integer_and",      "Leda_integer_not",      "Leda_integer_asReal", "Leda_object_allocate",
                                  "Leda_object_at",        "Leda_object_atPut",     "Leda_object_cast",    "Leda_string_length",
                                  "Leda_string_substring", "Leda_stdin_read",       "Leda_object_defined", "Leda_real_asString",
                                  "Leda_real_plus",        "Leda_real_minus",       "Leda_real_times",     "Leda_real_divide",
                                  "Leda_real_less",        "Leda_real_asInteger",   "Leda_real_equals"};
    size_t count = sizeof(names) / sizeof(names[0]);
    return index < count ? names[index] : NULL;
}

static const char *function_name(const struct bc_module *module, uint64_t index) {
    if(index >= module->function_count) { return NULL; }
    return module->functions[index].name;
}

static void print_const_ref(const struct bc_module *module, uint64_t index) {
    const struct bc_constant *constant;
    if(index >= module->constant_count) {
        printf(" ; invalid-constant[%" PRIu64 "]", index);
        return;
    }
    constant = &module->constants[index];
    switch(constant->kind) {
        case BC_CONST_INTEGER: printf(" ; const[%" PRIu64 "]=integer(%" PRId64 ")", index, constant->value.integer); break;
        case BC_CONST_REAL: printf(" ; const[%" PRIu64 "]=real(%g)", index, constant->value.real); break;
        case BC_CONST_STRING:
            printf(" ; const[%" PRIu64 "]=string(", index);
            print_escaped(constant->value.string);
            putchar(')');
            break;
        case BC_CONST_BOOLEAN:
            printf(" ; const[%" PRIu64 "]=boolean(%s)", index, constant->value.integer ? "true" : "false");
            break;
        case BC_CONST_FUNCTION:
            printf(" ; const[%" PRIu64 "]=closure(function=%" PRIu64, index, constant->value.closure.function_index);
            if(function_name(module, constant->value.closure.function_index) != NULL) {
                printf(" [%s]", function_name(module, constant->value.closure.function_index));
            }
            printf(", env=%" PRIu64 ")", constant->value.closure.parent_env_index);
            break;
        case BC_CONST_REFERENCE: printf(" ; const[%" PRIu64 "]=reference(runtime)", index); break;
        case BC_CONST_OBJECT: printf(" ; const[%" PRIu64 "]=object(runtime)", index); break;
        case BC_CONST_ENVREF:
            printf(" ; const[%" PRIu64 "]=envref(runtime:%" PRIu64 ")", index, constant->value.env_index);
            break;
    }
}

static void print_constants(const struct bc_module *module) {
    size_t index;
    printf("Constants (%zu)\n", module->constant_count);
    for(index = 0; index < module->constant_count; ++index) {
        printf("  [%zu] ", index);
        print_const_ref(module, (uint64_t)index);
        putchar('\n');
    }
    putchar('\n');
}

static int print_instruction(const struct bc_module *module, const struct bc_function *fn, size_t *ip) {
    size_t offset = *ip;
    uint8_t opcode;
    if(*ip >= fn->code.size) { return 0; }
    opcode = fn->code.data[(*ip)++];
    printf("    %04zu  %-18s", offset, op_name((enum bc_opcode)opcode));
    switch((enum bc_opcode)opcode) {
        case BC_OP_HALT:
        case BC_OP_POP:
        case BC_OP_DUP:
        case BC_OP_RETURN:
        case BC_OP_LOAD_REF: break;
        case BC_OP_CONST: {
            uint64_t index;
            if(!read_u64(fn, ip, &index)) { return 0; }
            printf(" %" PRIu64, index);
            print_const_ref(module, index);
            break;
        }
        case BC_OP_JUMP:
        case BC_OP_JUMP_IF_FALSE: {
            int64_t delta;
            if(!read_i64(fn, ip, &delta)) { return 0; }
            printf(" %" PRId64 " -> %" PRId64, delta, (int64_t)(*ip) + delta);
            break;
        }
        case BC_OP_CALL:
        case BC_OP_CALL_PRIMITIVE: {
            uint64_t a, b;
            if(!read_u64(fn, ip, &a) || !read_u64(fn, ip, &b)) { return 0; }
            if((enum bc_opcode)opcode == BC_OP_CALL) {
                uint64_t depth;
                if(!read_u64(fn, ip, &depth)) { return 0; }
                printf(" %" PRIu64 ", %" PRIu64 ", depth=%" PRIu64, a, b, depth);
                if(function_name(module, a) != NULL) { printf(" ; function[%" PRIu64 "]=%s", a, function_name(module, a)); }
            } else if(primitive_name(a) != NULL) {
                printf(" %" PRIu64 ", %" PRIu64, a, b);
                printf(" ; primitive[%" PRIu64 "]=%s", a, primitive_name(a));
            } else {
                printf(" %" PRIu64 ", %" PRIu64, a, b);
            }
            break;
        }
        case BC_OP_BUILD_INSTANCE: {
            uint64_t slot_count;
            uint64_t arg_count;
            if(!read_u64(fn, ip, &slot_count) || !read_u64(fn, ip, &arg_count)) { return 0; }
            printf(" %" PRIu64 ", %" PRIu64, slot_count, arg_count);
            break;
        }
        case BC_OP_LOAD_LOCAL:
        case BC_OP_STORE_LOCAL:
        case BC_OP_LOAD_ARG:
        case BC_OP_STORE_ARG:
        case BC_OP_CALL_CLOSURE:
        case BC_OP_MAKE_REF_LOCAL:
        case BC_OP_MAKE_REF_ARG:
        case BC_OP_LOAD_OBJECT_SLOT:
        case BC_OP_STORE_OBJECT_SLOT:
        case BC_OP_MAKE_REF_OBJECT_SLOT:
        case BC_OP_MAKE_METHOD: {
            uint64_t operand;
            if(!read_u64(fn, ip, &operand)) { return 0; }
            printf(" %" PRIu64, operand);
            break;
        }
        case BC_OP_MAKE_CLOSURE: {
            uint64_t function_index, depth;
            if(!read_u64(fn, ip, &function_index) || !read_u64(fn, ip, &depth)) { return 0; }
            printf(" function=%" PRIu64, function_index);
            if(function_name(module, function_index) != NULL) { printf(" [%s]", function_name(module, function_index)); }
            printf(", depth=%" PRIu64, depth);
            break;
        }
        case BC_OP_LOAD_CAPTURE_LOCAL:
        case BC_OP_STORE_CAPTURE_LOCAL:
        case BC_OP_LOAD_CAPTURE_ARG:
        case BC_OP_STORE_CAPTURE_ARG:
        case BC_OP_MAKE_REF_CAPTURE_LOCAL:
        case BC_OP_MAKE_REF_CAPTURE_ARG: {
            uint64_t depth, slot;
            if(!read_u64(fn, ip, &depth) || !read_u64(fn, ip, &slot)) { return 0; }
            printf(" depth=%" PRIu64 ", slot=%" PRIu64, depth, slot);
            break;
        }
        default: printf(" ; raw-opcode=%u", opcode); break;
    }
    putchar('\n');
    return 1;
}

static void print_function(const struct bc_module *module, size_t index) {
    const struct bc_function *fn = &module->functions[index];
    size_t ip = 0;
    printf("Function[%zu] %s\n", index, fn->name ? fn->name : "");
    printf("  arity=%u locals=%u max_stack=%u flags=%u code_size=%zu\n", fn->arity, fn->local_count, fn->max_stack, fn->flags,
           fn->code.size);
    while(ip < fn->code.size) {
        if(!print_instruction(module, fn, &ip)) {
            printf("    %04zu  <truncated operand stream>\n", ip);
            break;
        }
    }
    putchar('\n');
}

static void print_module(const struct bc_module *module) {
    size_t index;
    printf("LBC module\n");
    printf("  version=%u\n", module->version);
    printf("  entry_function=%u", module->entry_function);
    if(function_name(module, module->entry_function) != NULL) { printf(" [%s]", function_name(module, module->entry_function)); }
    putchar('\n');
    printf("  constant_count=%zu\n", module->constant_count);
    printf("  function_count=%zu\n\n", module->function_count);
    print_constants(module);
    for(index = 0; index < module->function_count; ++index) { print_function(module, index); }
}

int main(int argc, char **argv) {
    FILE *input;
    struct bc_module module;
    if(argc != 2) {
        usage(argv[0]);
        return 1;
    }
    input = fopen(argv[1], "rb");
    if(input == NULL) {
        fprintf(stderr, "lbcdump: unable to open %s\n", argv[1]);
        return 1;
    }
    if(!bc_module_read(input, &module)) {
        fprintf(stderr, "lbcdump: unable to read bytecode module from %s\n", argv[1]);
        fclose(input);
        return 1;
    }
    fclose(input);
    print_module(&module);
    bc_module_free(&module);
    return 0;
}
