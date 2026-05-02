#include "vm.h"

#include <stdio.h>
#include <string.h>

static void vm_set_error(char *error_buffer, size_t error_buffer_size, const char *message) {
    if(error_buffer != NULL && error_buffer_size > 0) { snprintf(error_buffer, error_buffer_size, "%s", message); }
}

static void vm_set_error2(char *error_buffer, size_t error_buffer_size, const char *format, size_t first, size_t second) {
    if(error_buffer != NULL && error_buffer_size > 0) { snprintf(error_buffer, error_buffer_size, format, first, second); }
}

static struct bc_frame *vm_current_frame(struct bc_vm *vm) { return &vm->frames[vm->current_frame_index]; }

static int vm_enter_frame(struct bc_vm *vm, struct bc_function *function, size_t arg_count, size_t caller_frame_index,
                          size_t closure_env_index, char *error_buffer, size_t error_buffer_size);

static int vm_promote_frame_environment(struct bc_vm *vm, size_t frame_index, size_t *env_index, char *error_buffer,
                                        size_t error_buffer_size) {
    struct bc_frame *frame;
    struct bc_environment *environment;
    size_t arg_index;
    size_t local_index;

    if(frame_index >= vm->frame_count) {
        vm_set_error(error_buffer, error_buffer_size, "invalid frame index for environment promotion");
        return 0;
    }

    frame = &vm->frames[frame_index];
    if(frame->promoted_env_index != SIZE_MAX) {
        *env_index = frame->promoted_env_index;
        return 1;
    }

    if(vm->environment_count >= BC_VM_MAX_ENVIRONMENTS) {
        vm_set_error(error_buffer, error_buffer_size, "environment storage exhausted");
        return 0;
    }

    environment = &vm->environments[vm->environment_count];
    memset(environment, 0, sizeof(*environment));
    environment->parent_env_index = frame->closure_env_index;
    environment->arg_base = 0;
    environment->local_base = frame->arg_count;
    environment->arg_count = frame->arg_count;
    environment->local_count = frame->local_count;

    if(vm->environment_count == 0) {
        environment->arg_base = 0;
    } else {
        struct bc_environment *previous = &vm->environments[vm->environment_count - 1];
        environment->arg_base = previous->local_base + previous->local_count;
    }
    environment->local_base = environment->arg_base + environment->arg_count;

    if(environment->local_base + environment->local_count > BC_VM_MAX_ENV_SLOTS) {
        vm_set_error(error_buffer, error_buffer_size, "environment slot storage exhausted");
        return 0;
    }

    for(arg_index = 0; arg_index < frame->arg_count; ++arg_index) {
        vm->env_slots[environment->arg_base + arg_index] = vm->frame_slots[frame->arg_base + arg_index];
    }
    for(local_index = 0; local_index < frame->local_count; ++local_index) {
        vm->env_slots[environment->local_base + local_index] = vm->frame_slots[frame->local_base + local_index];
    }

    frame->promoted_env_index = vm->environment_count;
    *env_index = vm->environment_count;
    vm->environment_count++;
    return 1;
}

static int vm_resolve_capture_env_index(struct bc_vm *vm, uint64_t depth, size_t *env_index, char *error_buffer,
                                        size_t error_buffer_size) {
    size_t resolved_env_index;

    if(depth == 0) {
        vm_set_error(error_buffer, error_buffer_size, "capture depth 0 is invalid for captured slot access");
        return 0;
    }

    resolved_env_index = vm_current_frame(vm)->closure_env_index;
    if(resolved_env_index == SIZE_MAX) {
        vm_set_error(error_buffer, error_buffer_size, "no captured environment available");
        return 0;
    }

    while(depth > 1) {
        struct bc_environment *environment;

        if(resolved_env_index >= vm->environment_count) {
            vm_set_error(error_buffer, error_buffer_size, "invalid captured environment index");
            return 0;
        }
        environment = &vm->environments[resolved_env_index];
        if(environment->parent_env_index == SIZE_MAX) {
            vm_set_error(error_buffer, error_buffer_size, "capture depth ran past available environments");
            return 0;
        }
        resolved_env_index = environment->parent_env_index;
        depth--;
    }

    *env_index = resolved_env_index;
    return 1;
}

static int vm_resolve_closure_env_index(struct bc_vm *vm, uint64_t context_depth, size_t *env_index, char *error_buffer,
                                        size_t error_buffer_size) {
    if(context_depth == 0) {
        return vm_promote_frame_environment(vm, vm->current_frame_index, env_index, error_buffer, error_buffer_size);
    }
    return vm_resolve_capture_env_index(vm, context_depth, env_index, error_buffer, error_buffer_size);
}

static int vm_get_local_slot_ref(struct bc_vm *vm, struct bc_frame *frame, uint64_t local_index, struct bc_constant ***slot_ref,
                                 char *error_buffer, size_t error_buffer_size) {
    if(local_index >= frame->local_count) {
        vm_set_error(error_buffer, error_buffer_size, "invalid local slot index");
        return 0;
    }

    if(frame->promoted_env_index != SIZE_MAX) {
        struct bc_environment *environment;
        size_t slot_index;

        if(frame->promoted_env_index >= vm->environment_count) {
            vm_set_error(error_buffer, error_buffer_size, "invalid promoted environment index");
            return 0;
        }

        environment = &vm->environments[frame->promoted_env_index];
        slot_index = environment->local_base + (size_t)local_index;
        if(slot_index >= BC_VM_MAX_ENV_SLOTS) {
            vm_set_error(error_buffer, error_buffer_size, "invalid promoted local slot access");
            return 0;
        }

        *slot_ref = &vm->env_slots[slot_index];
        return 1;
    }

    if(frame->local_base + (size_t)local_index >= BC_VM_MAX_FRAME_SLOTS) {
        vm_set_error(error_buffer, error_buffer_size, "invalid local slot access");
        return 0;
    }

    *slot_ref = &vm->frame_slots[frame->local_base + (size_t)local_index];
    return 1;
}

static int vm_get_arg_slot_ref(struct bc_vm *vm, struct bc_frame *frame, uint64_t arg_index, struct bc_constant ***slot_ref,
                               char *error_buffer, size_t error_buffer_size) {
    if(arg_index >= frame->arg_count) {
        vm_set_error2(error_buffer, error_buffer_size, "invalid current-frame argument slot index %zu (arg_count=%zu)",
                      (size_t)arg_index, (size_t)frame->arg_count);
        return 0;
    }

    if(frame->promoted_env_index != SIZE_MAX) {
        struct bc_environment *environment;
        size_t slot_index;

        if(frame->promoted_env_index >= vm->environment_count) {
            vm_set_error(error_buffer, error_buffer_size, "invalid promoted environment index");
            return 0;
        }

        environment = &vm->environments[frame->promoted_env_index];
        slot_index = environment->arg_base + (size_t)arg_index;
        if(slot_index >= BC_VM_MAX_ENV_SLOTS) {
            vm_set_error(error_buffer, error_buffer_size, "invalid promoted argument slot access");
            return 0;
        }

        *slot_ref = &vm->env_slots[slot_index];
        return 1;
    }

    if(frame->arg_base + (size_t)arg_index >= BC_VM_MAX_FRAME_SLOTS) {
        vm_set_error(error_buffer, error_buffer_size, "invalid argument slot access");
        return 0;
    }

    *slot_ref = &vm->frame_slots[frame->arg_base + (size_t)arg_index];
    return 1;
}

static int vm_get_capture_local_slot_ref(struct bc_vm *vm, uint64_t depth, uint64_t local_index, struct bc_constant ***slot_ref,
                                         char *error_buffer, size_t error_buffer_size) {
    size_t env_index;
    struct bc_environment *environment;
    size_t slot_index;

    if(!vm_resolve_capture_env_index(vm, depth, &env_index, error_buffer, error_buffer_size)) { return 0; }
    if(env_index >= vm->environment_count) {
        vm_set_error(error_buffer, error_buffer_size, "invalid captured environment index");
        return 0;
    }

    environment = &vm->environments[env_index];
    if(local_index >= environment->local_count) {
        vm_set_error(error_buffer, error_buffer_size, "invalid captured local slot index");
        return 0;
    }

    slot_index = environment->local_base + (size_t)local_index;
    if(slot_index >= BC_VM_MAX_ENV_SLOTS) {
        vm_set_error(error_buffer, error_buffer_size, "invalid captured local slot access");
        return 0;
    }

    *slot_ref = &vm->env_slots[slot_index];
    return 1;
}

static int vm_get_capture_arg_slot_ref(struct bc_vm *vm, uint64_t depth, uint64_t arg_index, struct bc_constant ***slot_ref,
                                       char *error_buffer, size_t error_buffer_size) {
    size_t env_index;
    struct bc_environment *environment;
    size_t slot_index;

    if(!vm_resolve_capture_env_index(vm, depth, &env_index, error_buffer, error_buffer_size)) { return 0; }
    if(env_index >= vm->environment_count) {
        vm_set_error(error_buffer, error_buffer_size, "invalid captured environment index");
        return 0;
    }

    environment = &vm->environments[env_index];
    if(arg_index >= environment->arg_count) {
        vm_set_error2(error_buffer, error_buffer_size, "invalid captured argument slot index %zu (arg_count=%zu)",
                      (size_t)arg_index, (size_t)environment->arg_count);
        return 0;
    }

    slot_index = environment->arg_base + (size_t)arg_index;
    if(slot_index >= BC_VM_MAX_ENV_SLOTS) {
        vm_set_error(error_buffer, error_buffer_size, "invalid captured argument slot access");
        return 0;
    }

    *slot_ref = &vm->env_slots[slot_index];
    return 1;
}

static struct bc_constant *vm_alloc_runtime_integer(struct bc_vm *vm, int64_t value, char *error_buffer,
                                                    size_t error_buffer_size) {
    struct bc_constant *constant;

    if(vm->runtime_constant_count >= BC_VM_MAX_RUNTIME_CONSTANTS) {
        vm_set_error(error_buffer, error_buffer_size, "runtime constant storage exhausted");
        return NULL;
    }

    constant = &vm->runtime_constants[vm->runtime_constant_count++];
    constant->kind = BC_CONST_INTEGER;
    constant->value.integer = value;
    return constant;
}

static struct bc_constant *vm_alloc_runtime_boolean(struct bc_vm *vm, int value, char *error_buffer, size_t error_buffer_size) {
    struct bc_constant *constant;

    if(vm->runtime_constant_count >= BC_VM_MAX_RUNTIME_CONSTANTS) {
        vm_set_error(error_buffer, error_buffer_size, "runtime constant storage exhausted");
        return NULL;
    }

    constant = &vm->runtime_constants[vm->runtime_constant_count++];
    constant->kind = BC_CONST_BOOLEAN;
    constant->value.integer = value ? 1 : 0;
    return constant;
}

static struct bc_constant *vm_alloc_runtime_function(struct bc_vm *vm, uint64_t function_index, uint64_t parent_env_index,
                                                     char *error_buffer, size_t error_buffer_size) {
    struct bc_constant *constant;

    if(vm->runtime_constant_count >= BC_VM_MAX_RUNTIME_CONSTANTS) {
        vm_set_error(error_buffer, error_buffer_size, "runtime constant storage exhausted");
        return NULL;
    }

    constant = &vm->runtime_constants[vm->runtime_constant_count++];
    constant->kind = BC_CONST_FUNCTION;
    constant->value.closure.function_index = function_index;
    constant->value.closure.parent_env_index = parent_env_index;
    return constant;
}

static int vm_validate_integer_primitive_args(struct bc_constant **argv, size_t argc, char *error_buffer,
                                              size_t error_buffer_size) {
    size_t index;

    for(index = 0; index < argc; ++index) {
        if(argv[index] == NULL || argv[index]->kind != BC_CONST_INTEGER) {
            vm_set_error(error_buffer, error_buffer_size, "primitive currently requires integer arguments");
            return 0;
        }
    }

    return 1;
}

static int vm_call_primitive(struct bc_vm *vm, uint64_t primitive_index, size_t argc, char *error_buffer,
                             size_t error_buffer_size) {
    struct bc_constant **argv;
    struct bc_constant *result;

    if(vm->stack_size < argc) {
        vm_set_error(error_buffer, error_buffer_size, "operand stack underflow in CALL_PRIMITIVE");
        return 0;
    }

    argv = &vm->stack[vm->stack_size - argc];
    switch(primitive_index) {
        case 4:
            if(argc != 2 || !vm_validate_integer_primitive_args(argv, argc, error_buffer, error_buffer_size)) { return 0; }
            result =
                vm_alloc_runtime_boolean(vm, argv[0]->value.integer == argv[1]->value.integer, error_buffer, error_buffer_size);
            break;

        case 5:
            if(argc != 2 || !vm_validate_integer_primitive_args(argv, argc, error_buffer, error_buffer_size)) { return 0; }
            result =
                vm_alloc_runtime_integer(vm, argv[0]->value.integer + argv[1]->value.integer, error_buffer, error_buffer_size);
            break;

        case 6:
            if(argc != 2 || !vm_validate_integer_primitive_args(argv, argc, error_buffer, error_buffer_size)) { return 0; }
            result =
                vm_alloc_runtime_integer(vm, argv[0]->value.integer - argv[1]->value.integer, error_buffer, error_buffer_size);
            break;

        case 7:
            if(argc != 2 || !vm_validate_integer_primitive_args(argv, argc, error_buffer, error_buffer_size)) { return 0; }
            result =
                vm_alloc_runtime_integer(vm, argv[0]->value.integer * argv[1]->value.integer, error_buffer, error_buffer_size);
            break;

        case 8:
            if(argc != 2 || !vm_validate_integer_primitive_args(argv, argc, error_buffer, error_buffer_size)) { return 0; }
            if(argv[1]->value.integer == 0) {
                vm_set_error(error_buffer, error_buffer_size, "division by zero in integer primitive");
                return 0;
            }
            result =
                vm_alloc_runtime_integer(vm, argv[0]->value.integer / argv[1]->value.integer, error_buffer, error_buffer_size);
            break;

        case 10:
            if(argc != 2 || !vm_validate_integer_primitive_args(argv, argc, error_buffer, error_buffer_size)) { return 0; }
            result =
                vm_alloc_runtime_boolean(vm, argv[0]->value.integer < argv[1]->value.integer, error_buffer, error_buffer_size);
            break;

        case 11:
            if(argc != 2 || !vm_validate_integer_primitive_args(argv, argc, error_buffer, error_buffer_size)) { return 0; }
            result =
                vm_alloc_runtime_integer(vm, argv[0]->value.integer | argv[1]->value.integer, error_buffer, error_buffer_size);
            break;

        case 12:
            if(argc != 2 || !vm_validate_integer_primitive_args(argv, argc, error_buffer, error_buffer_size)) { return 0; }
            result =
                vm_alloc_runtime_integer(vm, argv[0]->value.integer & argv[1]->value.integer, error_buffer, error_buffer_size);
            break;

        case 13:
            if(argc != 1 || !vm_validate_integer_primitive_args(argv, argc, error_buffer, error_buffer_size)) { return 0; }
            result = vm_alloc_runtime_integer(vm, ~argv[0]->value.integer, error_buffer, error_buffer_size);
            break;

        case 22:
            if(argc != 1) {
                vm_set_error(error_buffer, error_buffer_size, "defined primitive expects one argument");
                return 0;
            }
            result = vm_alloc_runtime_boolean(vm, argv[0] != NULL, error_buffer, error_buffer_size);
            break;

        default: vm_set_error(error_buffer, error_buffer_size, "primitive not implemented in VM yet"); return 0;
    }

    if(result == NULL) { return 0; }
    vm->stack_size -= argc;
    if(vm->stack_size >= BC_VM_MAX_STACK) {
        vm_set_error(error_buffer, error_buffer_size, "operand stack overflow after CALL_PRIMITIVE");
        return 0;
    }
    vm->stack[vm->stack_size++] = result;
    return 1;
}

static int vm_call_closure_value(struct bc_vm *vm, size_t argc, char *error_buffer, size_t error_buffer_size) {
    struct bc_constant *callee;
    uint64_t function_index;
    uint64_t parent_env_index;
    size_t callee_index;
    size_t i;
    size_t caller_frame_index = vm->current_frame_index;

    if(vm->stack_size < argc + 1) {
        vm_set_error(error_buffer, error_buffer_size, "operand stack underflow in CALL_CLOSURE");
        return 0;
    }

    callee_index = vm->stack_size - argc - 1;
    callee = vm->stack[callee_index];
    if(callee == NULL || callee->kind != BC_CONST_FUNCTION) {
        vm_set_error(error_buffer, error_buffer_size, "attempted to call a non-closure value");
        return 0;
    }

    function_index = callee->value.closure.function_index;
    parent_env_index = callee->value.closure.parent_env_index;
    if(function_index >= vm->module->function_count) {
        vm_set_error(error_buffer, error_buffer_size, "closure function index out of range");
        return 0;
    }

    for(i = 0; i < argc; ++i) { vm->stack[callee_index + i] = vm->stack[callee_index + i + 1]; }
    vm->stack_size--;

    return vm_enter_frame(vm, &vm->module->functions[function_index], argc, caller_frame_index, (size_t)parent_env_index,
                          error_buffer, error_buffer_size);
}

static int vm_enter_frame(struct bc_vm *vm, struct bc_function *function, size_t arg_count, size_t caller_frame_index,
                          size_t closure_env_index, char *error_buffer, size_t error_buffer_size) {
    struct bc_frame *frame;
    size_t arg_base;
    size_t local_base;
    size_t index;

    if(vm->frame_count >= BC_VM_MAX_FRAMES) {
        vm_set_error(error_buffer, error_buffer_size, "frame stack overflow");
        return 0;
    }

    if(function == NULL) {
        vm_set_error(error_buffer, error_buffer_size, "attempted to enter null function frame");
        return 0;
    }

    if(arg_count != function->arity) {
        vm_set_error(error_buffer, error_buffer_size, "argument count does not match function arity");
        return 0;
    }

    if(vm->stack_size < arg_count) {
        vm_set_error(error_buffer, error_buffer_size, "operand stack underflow while entering frame");
        return 0;
    }

    arg_base = vm->stack_size - arg_count;
    local_base = arg_base + arg_count;
    if(local_base + function->local_count > BC_VM_MAX_FRAME_SLOTS) {
        vm_set_error(error_buffer, error_buffer_size, "frame slot storage exhausted");
        return 0;
    }

    frame = &vm->frames[vm->frame_count];
    memset(frame, 0, sizeof(*frame));
    frame->function = function;
    frame->caller_frame_index = caller_frame_index;
    frame->closure_env_index = closure_env_index;
    frame->promoted_env_index = SIZE_MAX;
    frame->stack_base = arg_base;
    frame->arg_base = arg_base;
    frame->local_base = local_base;
    frame->arg_count = (uint32_t)arg_count;
    frame->local_count = function->local_count;

    for(index = 0; index < arg_count; ++index) { vm->frame_slots[arg_base + index] = vm->stack[arg_base + index]; }
    memset(&vm->frame_slots[local_base], 0, function->local_count * sizeof(vm->frame_slots[0]));
    vm->frame_count++;
    vm->current_frame_index = vm->frame_count - 1;
    return 1;
}

static int vm_leave_frame(struct bc_vm *vm, char *error_buffer, size_t error_buffer_size) {
    struct bc_frame *frame;
    struct bc_constant *result = NULL;

    if(vm->frame_count == 0) {
        vm_set_error(error_buffer, error_buffer_size, "frame stack underflow on return");
        return 0;
    }

    frame = vm_current_frame(vm);
    if(vm->stack_size > frame->stack_base) { result = vm->stack[vm->stack_size - 1]; }

    vm->stack_size = frame->stack_base;
    memset(&vm->frame_slots[frame->arg_base], 0, (frame->arg_count + frame->local_count) * sizeof(vm->frame_slots[0]));
    vm->frame_count--;

    if(vm->frame_count == 0) {
        if(result != NULL) {
            vm->stack[0] = result;
            vm->stack_size = 1;
        }
        return 1;
    }

    vm->current_frame_index = frame->caller_frame_index;
    if(result != NULL) {
        if(vm->stack_size >= BC_VM_MAX_STACK) {
            vm_set_error(error_buffer, error_buffer_size, "operand stack overflow while returning");
            return 0;
        }
        vm->stack[vm->stack_size++] = result;
    }

    return 1;
}

static int vm_read_u64le(struct bc_vm *vm, uint64_t *value) {
    struct bc_frame *frame = vm_current_frame(vm);
    uint64_t result = 0;
    size_t i;

    for(i = 0; i < 8; ++i) {
        uint8_t byte;

        if(frame->ip >= frame->function->code.size) { return 0; }
        byte = frame->function->code.data[frame->ip++];
        result |= ((uint64_t)byte) << (i * 8);
    }

    *value = result;
    return 1;
}

static int vm_read_i64le(struct bc_vm *vm, int64_t *value) {
    uint64_t raw = 0;

    if(!vm_read_u64le(vm, &raw)) { return 0; }
    memcpy(value, &raw, sizeof(*value));
    return 1;
}

static int vm_apply_jump_delta(struct bc_vm *vm, int64_t delta, char *error_buffer, size_t error_buffer_size,
                               const char *message) {
    struct bc_frame *frame = vm_current_frame(vm);
    int64_t new_ip = (int64_t)frame->ip + delta;

    if(new_ip < 0 || (size_t)new_ip > frame->function->code.size) {
        vm_set_error(error_buffer, error_buffer_size, message);
        return 0;
    }

    frame->ip = (size_t)new_ip;
    return 1;
}

static int vm_condition_is_false(struct bc_constant *condition, int *is_false, char *error_buffer, size_t error_buffer_size) {
    if(is_false == NULL) {
        vm_set_error(error_buffer, error_buffer_size, "internal error: missing condition result storage");
        return 0;
    }

    if(condition == NULL) {
        *is_false = 1;
        return 1;
    }

    switch(condition->kind) {
        case BC_CONST_BOOLEAN:
        case BC_CONST_INTEGER: *is_false = condition->value.integer == 0; return 1;

        default:
            vm_set_error(error_buffer, error_buffer_size, "branch condition must currently be a boolean-compatible value");
            return 0;
    }
}

int bc_vm_init(struct bc_vm *vm, struct bc_module *module) {
    if(module == NULL || module->function_count == 0 || module->entry_function >= module->function_count) { return 0; }

    vm->module = module;
    vm->stack_size = 0;
    memset(vm->frame_slots, 0, sizeof(vm->frame_slots));
    memset(vm->env_slots, 0, sizeof(vm->env_slots));
    memset(vm->runtime_constants, 0, sizeof(vm->runtime_constants));
    vm->runtime_constant_count = 0;
    memset(vm->environments, 0, sizeof(vm->environments));
    vm->environment_count = 0;
    memset(vm->frames, 0, sizeof(vm->frames));
    vm->frame_count = 0;
    vm->current_frame_index = 0;
    return vm_enter_frame(vm, &module->functions[module->entry_function], 0, 0, SIZE_MAX, NULL, 0);
}

int bc_vm_run(struct bc_vm *vm, char *error_buffer, size_t error_buffer_size) {
    for(;;) {
        struct bc_frame *frame = vm_current_frame(vm);
        uint8_t opcode;

        if(frame->ip >= frame->function->code.size) {
            vm_set_error(error_buffer, error_buffer_size, "instruction pointer ran past end of function");
            return 0;
        }

        opcode = frame->function->code.data[frame->ip++];
        switch((enum bc_opcode)opcode) {
            case BC_OP_HALT: return 1;

            case BC_OP_JUMP: {
                int64_t delta;
                if(!vm_read_i64le(vm, &delta)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed JUMP operand");
                    return 0;
                }
                if(!vm_apply_jump_delta(vm, delta, error_buffer, error_buffer_size, "jump target out of range")) { return 0; }
                break;
            }

            case BC_OP_JUMP_IF_FALSE: {
                int64_t delta;
                int is_false;
                struct bc_constant *condition;

                if(!vm_read_i64le(vm, &delta)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed JUMP_IF_FALSE operand");
                    return 0;
                }
                if(vm->stack_size == 0) {
                    vm_set_error(error_buffer, error_buffer_size, "stack underflow in JUMP_IF_FALSE");
                    return 0;
                }

                condition = vm->stack[--vm->stack_size];
                if(!vm_condition_is_false(condition, &is_false, error_buffer, error_buffer_size)) { return 0; }
                if(is_false
                   && !vm_apply_jump_delta(vm, delta, error_buffer, error_buffer_size, "conditional jump target out of range")) {
                    return 0;
                }
                break;
            }

            case BC_OP_CONST: {
                uint64_t constant_index;
                if(!vm_read_u64le(vm, &constant_index)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed CONST operand");
                    return 0;
                }
                if(constant_index >= vm->module->constant_count || vm->stack_size >= BC_VM_MAX_STACK) {
                    vm_set_error(error_buffer, error_buffer_size, "invalid constant access in CONST");
                    return 0;
                }
                vm->stack[vm->stack_size++] = &vm->module->constants[constant_index];
                break;
            }

            case BC_OP_POP:
                if(vm->stack_size == 0) {
                    vm_set_error(error_buffer, error_buffer_size, "stack underflow in POP");
                    return 0;
                }
                vm->stack_size--;
                break;

            case BC_OP_DUP:
                if(vm->stack_size == 0 || vm->stack_size >= BC_VM_MAX_STACK) {
                    vm_set_error(error_buffer, error_buffer_size, "invalid stack state in DUP");
                    return 0;
                }
                vm->stack[vm->stack_size] = vm->stack[vm->stack_size - 1];
                vm->stack_size++;
                break;

            case BC_OP_RETURN:
                if(!vm_leave_frame(vm, error_buffer, error_buffer_size)) { return 0; }
                if(vm->frame_count == 0) { return 1; }
                break;

            case BC_OP_CALL: {
                uint64_t function_index;
                uint64_t argument_count;
                size_t caller_frame_index = vm->current_frame_index;
                if(!vm_read_u64le(vm, &function_index) || !vm_read_u64le(vm, &argument_count)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed CALL operand");
                    return 0;
                }
                if(function_index >= vm->module->function_count) {
                    vm_set_error(error_buffer, error_buffer_size, "invalid function index in CALL");
                    return 0;
                }
                if(!vm_enter_frame(vm, &vm->module->functions[function_index], (size_t)argument_count, caller_frame_index,
                                   SIZE_MAX, error_buffer, error_buffer_size)) {
                    return 0;
                }
                break;
            }

            case BC_OP_MAKE_CLOSURE: {
                uint64_t function_index;
                uint64_t context_depth;
                size_t parent_env_index;
                struct bc_constant *closure;
                if(!vm_read_u64le(vm, &function_index) || !vm_read_u64le(vm, &context_depth)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed MAKE_CLOSURE operand");
                    return 0;
                }
                if(function_index >= vm->module->function_count || vm->stack_size >= BC_VM_MAX_STACK) {
                    vm_set_error(error_buffer, error_buffer_size, "invalid function index in MAKE_CLOSURE");
                    return 0;
                }
                if(!vm_resolve_closure_env_index(vm, context_depth, &parent_env_index, error_buffer, error_buffer_size)) {
                    return 0;
                }
                closure =
                    vm_alloc_runtime_function(vm, function_index, (uint64_t)parent_env_index, error_buffer, error_buffer_size);
                if(closure == NULL) { return 0; }
                vm->stack[vm->stack_size++] = closure;
                break;
            }

            case BC_OP_CALL_CLOSURE: {
                uint64_t argument_count;
                if(!vm_read_u64le(vm, &argument_count)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed CALL_CLOSURE operand");
                    return 0;
                }
                if(!vm_call_closure_value(vm, (size_t)argument_count, error_buffer, error_buffer_size)) { return 0; }
                break;
            }

            case BC_OP_CALL_PRIMITIVE: {
                uint64_t primitive_index;
                uint64_t argument_count;
                if(!vm_read_u64le(vm, &primitive_index) || !vm_read_u64le(vm, &argument_count)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed CALL_PRIMITIVE operand");
                    return 0;
                }
                if(!vm_call_primitive(vm, primitive_index, (size_t)argument_count, error_buffer, error_buffer_size)) { return 0; }
                break;
            }

            case BC_OP_LOAD_LOCAL: {
                uint64_t local_index;
                struct bc_constant **slot_ref;
                if(!vm_read_u64le(vm, &local_index)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed LOAD_LOCAL operand");
                    return 0;
                }
                frame = vm_current_frame(vm);
                if(vm->stack_size >= BC_VM_MAX_STACK
                   || !vm_get_local_slot_ref(vm, frame, local_index, &slot_ref, error_buffer, error_buffer_size)) {
                    if(vm->stack_size >= BC_VM_MAX_STACK) { vm_set_error(error_buffer, error_buffer_size, "invalid local load"); }
                    return 0;
                }
                vm->stack[vm->stack_size++] = *slot_ref;
                break;
            }

            case BC_OP_LOAD_CAPTURE_LOCAL: {
                uint64_t depth;
                uint64_t local_index;
                struct bc_constant **slot_ref;
                if(!vm_read_u64le(vm, &depth) || !vm_read_u64le(vm, &local_index)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed LOAD_CAPTURE_LOCAL operand");
                    return 0;
                }
                if(vm->stack_size >= BC_VM_MAX_STACK
                   || !vm_get_capture_local_slot_ref(vm, depth, local_index, &slot_ref, error_buffer, error_buffer_size)) {
                    if(vm->stack_size >= BC_VM_MAX_STACK) {
                        vm_set_error(error_buffer, error_buffer_size, "invalid captured local load");
                    }
                    return 0;
                }
                vm->stack[vm->stack_size++] = *slot_ref;
                break;
            }

            case BC_OP_STORE_LOCAL: {
                uint64_t local_index;
                struct bc_constant **slot_ref;
                if(!vm_read_u64le(vm, &local_index)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed STORE_LOCAL operand");
                    return 0;
                }
                frame = vm_current_frame(vm);
                if(vm->stack_size == 0
                   || !vm_get_local_slot_ref(vm, frame, local_index, &slot_ref, error_buffer, error_buffer_size)) {
                    if(vm->stack_size == 0) { vm_set_error(error_buffer, error_buffer_size, "invalid local store"); }
                    return 0;
                }
                *slot_ref = vm->stack[vm->stack_size - 1];
                break;
            }

            case BC_OP_STORE_CAPTURE_LOCAL: {
                uint64_t depth;
                uint64_t local_index;
                struct bc_constant **slot_ref;
                if(!vm_read_u64le(vm, &depth) || !vm_read_u64le(vm, &local_index)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed STORE_CAPTURE_LOCAL operand");
                    return 0;
                }
                if(vm->stack_size == 0
                   || !vm_get_capture_local_slot_ref(vm, depth, local_index, &slot_ref, error_buffer, error_buffer_size)) {
                    if(vm->stack_size == 0) { vm_set_error(error_buffer, error_buffer_size, "invalid captured local store"); }
                    return 0;
                }
                *slot_ref = vm->stack[vm->stack_size - 1];
                break;
            }

            case BC_OP_LOAD_ARG: {
                uint64_t arg_index;
                struct bc_constant **slot_ref;
                if(!vm_read_u64le(vm, &arg_index)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed LOAD_ARG operand");
                    return 0;
                }
                frame = vm_current_frame(vm);
                if(vm->stack_size >= BC_VM_MAX_STACK
                   || !vm_get_arg_slot_ref(vm, frame, arg_index, &slot_ref, error_buffer, error_buffer_size)) {
                    if(vm->stack_size >= BC_VM_MAX_STACK) {
                        vm_set_error(error_buffer, error_buffer_size, "invalid argument load");
                    }
                    return 0;
                }
                vm->stack[vm->stack_size++] = *slot_ref;
                break;
            }

            case BC_OP_LOAD_CAPTURE_ARG: {
                uint64_t depth;
                uint64_t arg_index;
                struct bc_constant **slot_ref;
                if(!vm_read_u64le(vm, &depth) || !vm_read_u64le(vm, &arg_index)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed LOAD_CAPTURE_ARG operand");
                    return 0;
                }
                if(vm->stack_size >= BC_VM_MAX_STACK
                   || !vm_get_capture_arg_slot_ref(vm, depth, arg_index, &slot_ref, error_buffer, error_buffer_size)) {
                    if(vm->stack_size >= BC_VM_MAX_STACK) {
                        vm_set_error(error_buffer, error_buffer_size, "invalid captured argument load");
                    }
                    return 0;
                }
                vm->stack[vm->stack_size++] = *slot_ref;
                break;
            }

            case BC_OP_STORE_ARG: {
                uint64_t arg_index;
                struct bc_constant **slot_ref;
                if(!vm_read_u64le(vm, &arg_index)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed STORE_ARG operand");
                    return 0;
                }
                frame = vm_current_frame(vm);
                if(vm->stack_size == 0
                   || !vm_get_arg_slot_ref(vm, frame, arg_index, &slot_ref, error_buffer, error_buffer_size)) {
                    if(vm->stack_size == 0) { vm_set_error(error_buffer, error_buffer_size, "invalid argument store"); }
                    return 0;
                }
                *slot_ref = vm->stack[vm->stack_size - 1];
                break;
            }

            case BC_OP_STORE_CAPTURE_ARG: {
                uint64_t depth;
                uint64_t arg_index;
                struct bc_constant **slot_ref;
                if(!vm_read_u64le(vm, &depth) || !vm_read_u64le(vm, &arg_index)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed STORE_CAPTURE_ARG operand");
                    return 0;
                }
                if(vm->stack_size == 0
                   || !vm_get_capture_arg_slot_ref(vm, depth, arg_index, &slot_ref, error_buffer, error_buffer_size)) {
                    if(vm->stack_size == 0) { vm_set_error(error_buffer, error_buffer_size, "invalid captured argument store"); }
                    return 0;
                }
                *slot_ref = vm->stack[vm->stack_size - 1];
                break;
            }

            default: vm_set_error(error_buffer, error_buffer_size, "opcode not implemented in VM yet"); return 0;
        }
    }
}