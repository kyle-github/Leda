#include "bytecode.h"

#include <stdlib.h>
#include <string.h>

static uint64_t bc_double_to_bits(double value) {
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static double bc_bits_to_double(uint64_t bits) {
    double value = 0.0;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static char *bc_strdup(const char *text) {
    size_t len;
    char *copy;

    if(text == NULL) { return NULL; }

    len = strlen(text);
    copy = (char *)malloc(len + 1);
    if(copy == NULL) { return NULL; }

    memcpy(copy, text, len + 1);
    return copy;
}

static int bc_buffer_reserve(struct bc_buffer *buffer, size_t needed) {
    uint8_t *new_data;
    size_t new_capacity;

    if(buffer->capacity >= needed) { return 1; }

    new_capacity = buffer->capacity ? buffer->capacity : 32;
    while(new_capacity < needed) { new_capacity *= 2; }

    new_data = (uint8_t *)realloc(buffer->data, new_capacity);
    if(new_data == NULL) { return 0; }

    buffer->data = new_data;
    buffer->capacity = new_capacity;
    return 1;
}

static int bc_write_u8(FILE *output, uint8_t value) { return fwrite(&value, sizeof(value), 1, output) == 1; }

static int bc_write_u64le(FILE *output, uint64_t value) {
    size_t i;

    for(i = 0; i < 8; ++i) {
        if(!bc_write_u8(output, (uint8_t)((value >> (i * 8)) & 0xffu))) { return 0; }
    }

    return 1;
}

static int bc_read_u8(FILE *input, uint8_t *value) { return fread(value, sizeof(*value), 1, input) == 1; }

static int bc_write_i64le(FILE *output, int64_t value) {
    uint64_t raw = 0;
    memcpy(&raw, &value, sizeof(raw));
    return bc_write_u64le(output, raw);
}

static int bc_read_u64le(FILE *input, uint64_t *value) {
    size_t i;
    uint64_t result = 0;

    for(i = 0; i < 8; ++i) {
        uint8_t byte;
        if(!bc_read_u8(input, &byte)) { return 0; }
        result |= ((uint64_t)byte) << (i * 8);
    }

    *value = result;
    return 1;
}

static int bc_read_i64le(FILE *input, int64_t *value) {
    uint64_t raw = 0;
    if(!bc_read_u64le(input, &raw)) { return 0; }
    memcpy(value, &raw, sizeof(*value));
    return 1;
}

void bc_module_init(struct bc_module *module) {
    memset(module, 0, sizeof(*module));
    module->version = BC_VERSION;
}

void bc_module_free(struct bc_module *module) {
    size_t i;

    for(i = 0; i < module->constant_count; ++i) {
        if(module->constants[i].kind == BC_CONST_STRING) { free(module->constants[i].value.string); }
    }
    free(module->constants);

    for(i = 0; i < module->function_count; ++i) {
        free(module->functions[i].name);
        free(module->functions[i].code.data);
    }
    free(module->functions);

    bc_module_init(module);
}

static int bc_ensure_constant_capacity(struct bc_module *module, size_t needed) {
    struct bc_constant *new_constants;
    size_t new_capacity;

    if(module->constant_capacity >= needed) { return 1; }
    new_capacity = module->constant_capacity ? module->constant_capacity : 8;
    while(new_capacity < needed) { new_capacity *= 2; }

    new_constants = (struct bc_constant *)realloc(module->constants, new_capacity * sizeof(*new_constants));
    if(new_constants == NULL) { return 0; }

    module->constants = new_constants;
    module->constant_capacity = new_capacity;
    return 1;
}

static int bc_ensure_function_capacity(struct bc_module *module, size_t needed) {
    struct bc_function *new_functions;
    size_t new_capacity;

    if(module->function_capacity >= needed) { return 1; }
    new_capacity = module->function_capacity ? module->function_capacity : 4;
    while(new_capacity < needed) { new_capacity *= 2; }

    new_functions = (struct bc_function *)realloc(module->functions, new_capacity * sizeof(*new_functions));
    if(new_functions == NULL) { return 0; }

    module->functions = new_functions;
    module->function_capacity = new_capacity;
    return 1;
}

size_t bc_add_integer_constant(struct bc_module *module, int64_t value) {
    size_t index = module->constant_count;
    if(!bc_ensure_constant_capacity(module, index + 1)) { return (size_t)-1; }
    module->constants[index].kind = BC_CONST_INTEGER;
    module->constants[index].value.integer = value;
    module->constant_count++;
    return index;
}

size_t bc_add_real_constant(struct bc_module *module, double value) {
    size_t index = module->constant_count;
    if(!bc_ensure_constant_capacity(module, index + 1)) { return (size_t)-1; }
    module->constants[index].kind = BC_CONST_REAL;
    module->constants[index].value.real = value;
    module->constant_count++;
    return index;
}

size_t bc_add_string_constant(struct bc_module *module, const char *value) {
    size_t index = module->constant_count;
    if(!bc_ensure_constant_capacity(module, index + 1)) { return (size_t)-1; }
    module->constants[index].kind = BC_CONST_STRING;
    module->constants[index].value.string = bc_strdup(value ? value : "");
    if(module->constants[index].value.string == NULL) { return (size_t)-1; }
    module->constant_count++;
    return index;
}

size_t bc_add_function(struct bc_module *module, const char *name, uint32_t arity, uint32_t local_count, uint32_t max_stack,
                       uint32_t flags) {
    struct bc_function *function;
    size_t index = module->function_count;
    if(!bc_ensure_function_capacity(module, index + 1)) { return (size_t)-1; }

    function = &module->functions[index];
    memset(function, 0, sizeof(*function));
    function->name = bc_strdup(name ? name : "");
    if(function->name == NULL) { return (size_t)-1; }
    function->arity = arity;
    function->local_count = local_count;
    function->max_stack = max_stack;
    function->flags = flags;
    module->function_count++;
    return index;
}

int bc_emit_u8(struct bc_function *function, uint8_t value) {
    if(!bc_buffer_reserve(&function->code, function->code.size + 1)) { return 0; }
    function->code.data[function->code.size++] = value;
    return 1;
}

int bc_emit_opcode(struct bc_function *function, enum bc_opcode opcode) { return bc_emit_u8(function, (uint8_t)opcode); }

int bc_emit_u64le(struct bc_function *function, uint64_t value) {
    size_t i;

    for(i = 0; i < 8; ++i) {
        if(!bc_emit_u8(function, (uint8_t)((value >> (i * 8)) & 0xffu))) { return 0; }
    }

    return 1;
}

int bc_emit_i64le(struct bc_function *function, int64_t value) {
    uint64_t raw = 0;
    memcpy(&raw, &value, sizeof(raw));
    return bc_emit_u64le(function, raw);
}

int bc_module_write(FILE *output, const struct bc_module *module) {
    size_t i;

    if(fwrite(BC_MAGIC, 1, 4, output) != 4) { return 0; }
    if(!bc_write_u64le(output, module->version) || !bc_write_u64le(output, module->entry_function)
       || !bc_write_u64le(output, module->constant_count) || !bc_write_u64le(output, module->function_count)) {
        return 0;
    }

    for(i = 0; i < module->constant_count; ++i) {
        const struct bc_constant *constant = &module->constants[i];
        size_t len;

        if(!bc_write_u8(output, (uint8_t)constant->kind)) { return 0; }

        switch(constant->kind) {
            case BC_CONST_INTEGER:
                if(!bc_write_i64le(output, constant->value.integer)) { return 0; }
                break;

            case BC_CONST_BOOLEAN:
                if(!bc_write_i64le(output, constant->value.integer ? 1 : 0)) { return 0; }
                break;

            case BC_CONST_REAL:
                for(len = 0; len < 8; ++len) {
                    uint64_t raw_bits = bc_double_to_bits(constant->value.real);
                    uint8_t byte = (uint8_t)((raw_bits >> (len * 8)) & 0xffu);
                    if(!bc_write_u8(output, byte)) { return 0; }
                }
                break;

            case BC_CONST_STRING:
                len = strlen(constant->value.string);
                if(!bc_write_u64le(output, len) || fwrite(constant->value.string, 1, len, output) != len) { return 0; }
                break;

            default: return 0;
        }
    }

    for(i = 0; i < module->function_count; ++i) {
        const struct bc_function *function = &module->functions[i];
        size_t len = strlen(function->name);
        if(!bc_write_u64le(output, len) || fwrite(function->name, 1, len, output) != len
           || !bc_write_u64le(output, function->arity) || !bc_write_u64le(output, function->local_count)
           || !bc_write_u64le(output, function->max_stack) || !bc_write_u64le(output, function->flags)
           || !bc_write_u64le(output, function->code.size)
           || fwrite(function->code.data, 1, function->code.size, output) != function->code.size) {
            return 0;
        }
    }

    return 1;
}

int bc_module_read(FILE *input, struct bc_module *module) {
    char magic[4];
    uint64_t value;
    size_t i;

    bc_module_init(module);
    if(fread(magic, 1, 4, input) != 4 || memcmp(magic, BC_MAGIC, 4) != 0) { return 0; }

    if(!bc_read_u64le(input, &value)) { return 0; }
    module->version = (uint32_t)value;
    if(!bc_read_u64le(input, &value)) { return 0; }
    module->entry_function = (uint32_t)value;
    if(!bc_read_u64le(input, &value)) { return 0; }
    if(!bc_ensure_constant_capacity(module, (size_t)value)) { return 0; }
    module->constant_count = (size_t)value;
    if(!bc_read_u64le(input, &value)) { return 0; }
    if(!bc_ensure_function_capacity(module, (size_t)value)) { return 0; }
    module->function_count = (size_t)value;

    for(i = 0; i < module->constant_count; ++i) {
        uint8_t kind;
        if(!bc_read_u8(input, &kind)) {
            bc_module_free(module);
            return 0;
        }
        module->constants[i].kind = (enum bc_constant_kind)kind;
        switch(module->constants[i].kind) {
            case BC_CONST_INTEGER: {
                int64_t signed_value;
                if(!bc_read_i64le(input, &signed_value)) {
                    bc_module_free(module);
                    return 0;
                }
                module->constants[i].value.integer = signed_value;
                break;
            }

            case BC_CONST_BOOLEAN: {
                int64_t signed_value;
                if(!bc_read_i64le(input, &signed_value)) {
                    bc_module_free(module);
                    return 0;
                }
                module->constants[i].value.integer = signed_value ? 1 : 0;
                break;
            }

            case BC_CONST_REAL: {
                uint64_t raw_bits = 0;
                size_t j;
                for(j = 0; j < 8; ++j) {
                    uint8_t byte;
                    if(!bc_read_u8(input, &byte)) {
                        bc_module_free(module);
                        return 0;
                    }
                    raw_bits |= ((uint64_t)byte) << (j * 8);
                }
                module->constants[i].value.real = bc_bits_to_double(raw_bits);
                break;
            }

            case BC_CONST_STRING: {
                size_t len;
                if(!bc_read_u64le(input, &value)) {
                    bc_module_free(module);
                    return 0;
                }
                len = (size_t)value;
                module->constants[i].value.string = (char *)malloc(len + 1);
                if(module->constants[i].value.string == NULL || fread(module->constants[i].value.string, 1, len, input) != len) {
                    bc_module_free(module);
                    return 0;
                }
                module->constants[i].value.string[len] = '\0';
                break;
            }

            default: bc_module_free(module); return 0;
        }
    }

    for(i = 0; i < module->function_count; ++i) {
        struct bc_function *function = &module->functions[i];
        size_t len;

        memset(function, 0, sizeof(*function));

        if(!bc_read_u64le(input, &value)) {
            bc_module_free(module);
            return 0;
        }
        len = (size_t)value;
        function->name = (char *)malloc(len + 1);
        if(function->name == NULL || fread(function->name, 1, len, input) != len) {
            bc_module_free(module);
            return 0;
        }
        function->name[len] = '\0';

        if(!bc_read_u64le(input, &value)) {
            bc_module_free(module);
            return 0;
        }
        function->arity = (uint32_t)value;
        if(!bc_read_u64le(input, &value)) {
            bc_module_free(module);
            return 0;
        }
        function->local_count = (uint32_t)value;
        if(!bc_read_u64le(input, &value)) {
            bc_module_free(module);
            return 0;
        }
        function->max_stack = (uint32_t)value;
        if(!bc_read_u64le(input, &value)) {
            bc_module_free(module);
            return 0;
        }
        function->flags = (uint32_t)value;
        if(!bc_read_u64le(input, &value)) {
            bc_module_free(module);
            return 0;
        }
        function->code.size = (size_t)value;
        function->code.capacity = function->code.size;
        function->code.data = (uint8_t *)malloc(function->code.size ? function->code.size : 1);
        if(function->code.data == NULL || fread(function->code.data, 1, function->code.size, input) != function->code.size) {
            bc_module_free(module);
            return 0;
        }
    }

    return 1;
}