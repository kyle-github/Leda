#ifndef LEDA_VM_H
#define LEDA_VM_H

#include "bytecode.h"

#define BC_VM_MAX_STACK 256u
#define BC_VM_MAX_FRAME_SLOTS 512u
#define BC_VM_MAX_FRAMES 64u
#define BC_VM_MAX_ENVIRONMENTS 128u
#define BC_VM_MAX_ENV_SLOTS 1024u
#define BC_VM_MAX_RUNTIME_CONSTANTS 256u

struct bc_environment {
    size_t parent_env_index;
    size_t arg_base;
    size_t local_base;
    uint32_t arg_count;
    uint32_t local_count;
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

struct bc_vm {
    struct bc_module *module;
    struct bc_constant *stack[BC_VM_MAX_STACK];
    size_t stack_size;
    struct bc_constant *frame_slots[BC_VM_MAX_FRAME_SLOTS];
    struct bc_constant *env_slots[BC_VM_MAX_ENV_SLOTS];
    struct bc_constant runtime_constants[BC_VM_MAX_RUNTIME_CONSTANTS];
    size_t runtime_constant_count;
    struct bc_environment environments[BC_VM_MAX_ENVIRONMENTS];
    size_t environment_count;
    struct bc_frame frames[BC_VM_MAX_FRAMES];
    size_t frame_count;
    size_t current_frame_index;
};

int bc_vm_init(struct bc_vm *vm, struct bc_module *module);
int bc_vm_run(struct bc_vm *vm, char *error_buffer, size_t error_buffer_size);

#endif