#ifndef LEDA_VM_H
#define LEDA_VM_H

#include "bytecode.h"

#define BC_VM_MAX_STACK 4096u
#define BC_VM_MAX_FRAME_SLOTS 8192u
#define BC_VM_MAX_FRAMES 512u
#define BC_VM_MAX_ENVIRONMENTS 4096u
#define BC_VM_MAX_ENV_SLOTS 65536u
#define BC_VM_MAX_RUNTIME_CONSTANTS 65536u

struct bc_environment {
    size_t parent_env_index;
    size_t arg_base;
    size_t local_base;
    uint32_t arg_count;
    uint32_t local_count;
    struct bc_constant *object;
    struct bc_constant *self;
};

struct bc_frame {
    struct bc_function *function;
    size_t ip;
    size_t caller_frame_index;
    size_t closure_env_index;
    size_t promoted_env_index;
    size_t stack_base;
    size_t arg_base;
    size_t local_base;
    uint32_t arg_count;
    uint32_t local_count;
};

#define BC_BUILTIN_INTEGER 0u
#define BC_BUILTIN_STRING  1u
#define BC_BUILTIN_BOOLEAN 2u
#define BC_BUILTIN_REAL    3u
#define BC_BUILTIN_TRUE    4u
#define BC_BUILTIN_FALSE   5u
#define BC_BUILTIN_COUNT   6u

struct bc_vm {
    struct bc_module *module;
    struct bc_constant *builtin_class_tables[BC_BUILTIN_COUNT];
    struct bc_constant *stack[BC_VM_MAX_STACK];
    size_t stack_size;
    struct bc_constant *frame_slots[BC_VM_MAX_FRAME_SLOTS];
    struct bc_constant *env_slots[BC_VM_MAX_ENV_SLOTS];
    struct bc_constant runtime_constants[BC_VM_MAX_RUNTIME_CONSTANTS];
    size_t runtime_constant_count;
    struct bc_environment environments[BC_VM_MAX_ENVIRONMENTS];
    size_t environment_count;
    size_t env_slot_count;   /* next free index in env_slots[] for promoted frame envs */
    struct bc_frame frames[BC_VM_MAX_FRAMES];
    size_t frame_count;
    size_t current_frame_index;
};

int bc_vm_init(struct bc_vm *vm, struct bc_module *module);
int bc_vm_run(struct bc_vm *vm, char *error_buffer, size_t error_buffer_size);
void bc_vm_free(struct bc_vm *vm);

#endif