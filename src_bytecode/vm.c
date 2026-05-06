#include "vm.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
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
static int vm_get_object_slot_ref(struct bc_constant *object, uint64_t slot_index, struct bc_constant ***slot_ref,
                                  char *error_buffer, size_t error_buffer_size);

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
    environment->arg_count = frame->arg_count;
    environment->local_count = frame->local_count;
    environment->object = NULL;

    /* Use env_slot_count as the watermark so primitive/object envs (which have
       local_base=0, local_count=0) don't cause overlap with existing promoted envs. */
    environment->arg_base = vm->env_slot_count;
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

    vm->env_slot_count = environment->local_base + environment->local_count;

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
            char msg[128];
            snprintf(msg, sizeof(msg), "capture depth ran past (fn=%s depth_rem=%llu env=%zu)",
                     vm_current_frame(vm)->function->name ? vm_current_frame(vm)->function->name : "?",
                     (unsigned long long)depth, resolved_env_index);
            vm_set_error(error_buffer, error_buffer_size, msg);
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
        /* Thunks/lambdas captured via depth=0 emit LOAD_LOCAL to access outer context slots:
           slots 0-3 are outer locals, slots 4+ are outer args (arg_slot = local_index - 4). */
        if(frame->closure_env_index != SIZE_MAX && frame->closure_env_index < vm->environment_count) {
            struct bc_environment *closure_env = &vm->environments[frame->closure_env_index];
            if(closure_env->object != NULL) {
                return vm_get_object_slot_ref(closure_env->object, local_index, slot_ref, error_buffer,
                                              error_buffer_size);
            }
            if(local_index < 4) {
                if(local_index < closure_env->local_count) {
                    size_t slot_index = closure_env->local_base + (size_t)local_index;
                    if(slot_index < BC_VM_MAX_ENV_SLOTS) {
                        *slot_ref = &vm->env_slots[slot_index];
                        return 1;
                    }
                }
            } else {
                size_t arg_slot = (size_t)(local_index - 4);
                if(arg_slot < closure_env->arg_count) {
                    size_t slot_index = closure_env->arg_base + arg_slot;
                    if(slot_index < BC_VM_MAX_ENV_SLOTS) {
                        *slot_ref = &vm->env_slots[slot_index];
                        return 1;
                    }
                }
            }
        }
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
    if(environment->object != NULL) {
        /* Slot 1 in an object-method environment is 'self' (the receiver object),
           matching the convention used by the primitive wrapper (vm_bind_primitive_environment). */
        if(local_index == 1 && environment->self != NULL) {
            *slot_ref = &environment->self;
            return 1;
        }
        return vm_get_object_slot_ref(environment->object, local_index, slot_ref, error_buffer, error_buffer_size);
    }
    if(local_index >= environment->local_count) {
        /* Slots 4+ map to captured args (arg_slot = local_index - 4), mirroring the
           convention in vm_get_local_slot_ref for thunks/lambdas. */
        if(local_index >= 4) {
            size_t arg_slot = (size_t)(local_index - 4);
            if(arg_slot < environment->arg_count) {
                slot_index = environment->arg_base + arg_slot;
                if(slot_index < BC_VM_MAX_ENV_SLOTS) {
                    *slot_ref = &vm->env_slots[slot_index];
                    return 1;
                }
            }
        }
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

static struct bc_constant *vm_alloc_runtime_string(struct bc_vm *vm, const char *value, char *error_buffer,
                                                   size_t error_buffer_size) {
    struct bc_constant *constant;

    if(vm->runtime_constant_count >= BC_VM_MAX_RUNTIME_CONSTANTS) {
        vm_set_error(error_buffer, error_buffer_size, "runtime constant storage exhausted");
        return NULL;
    }

    constant = &vm->runtime_constants[vm->runtime_constant_count++];
    constant->kind = BC_CONST_STRING;
    constant->value.string = strdup(value);
    if(constant->value.string == NULL) {
        constant->kind = 0;
        vm->runtime_constant_count--;
        vm_set_error(error_buffer, error_buffer_size, "runtime string allocation failed");
        return NULL;
    }
    return constant;
}

static struct bc_constant *vm_alloc_runtime_real(struct bc_vm *vm, double value, char *error_buffer,
                                                 size_t error_buffer_size) {
    struct bc_constant *constant;

    if(vm->runtime_constant_count >= BC_VM_MAX_RUNTIME_CONSTANTS) {
        vm_set_error(error_buffer, error_buffer_size, "runtime constant storage exhausted");
        return NULL;
    }

    constant = &vm->runtime_constants[vm->runtime_constant_count++];
    constant->kind = BC_CONST_REAL;
    constant->value.real = value;
    return constant;
}

static int vm_extract_real_value(struct bc_constant *value, double *out, char *error_buffer, size_t error_buffer_size) {
    if(value == NULL) {
        vm_set_error(error_buffer, error_buffer_size, "real primitive received undefined value");
        return 0;
    }
    if(value->kind == BC_CONST_REAL) {
        *out = value->value.real;
        return 1;
    }
    if(value->kind == BC_CONST_INTEGER) {
        *out = (double)value->value.integer;
        return 1;
    }
    vm_set_error(error_buffer, error_buffer_size, "real primitive expects real or integer argument");
    return 0;
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

static struct bc_constant *vm_alloc_runtime_reference(struct bc_vm *vm, struct bc_constant **slot_ref, char *error_buffer,
                                                      size_t error_buffer_size) {
    struct bc_constant *constant;

    if(vm->runtime_constant_count >= BC_VM_MAX_RUNTIME_CONSTANTS) {
        vm_set_error(error_buffer, error_buffer_size, "runtime constant storage exhausted");
        return NULL;
    }

    constant = &vm->runtime_constants[vm->runtime_constant_count++];
    constant->kind = BC_CONST_REFERENCE;
    constant->value.slot_ref = slot_ref;
    return constant;
}

static struct bc_constant *vm_alloc_runtime_envref(struct bc_vm *vm, uint64_t env_index, char *error_buffer,
                                                   size_t error_buffer_size) {
    struct bc_constant *constant;

    if(vm->runtime_constant_count >= BC_VM_MAX_RUNTIME_CONSTANTS) {
        vm_set_error(error_buffer, error_buffer_size, "runtime constant storage exhausted");
        return NULL;
    }

    constant = &vm->runtime_constants[vm->runtime_constant_count++];
    constant->kind = BC_CONST_ENVREF;
    constant->value.env_index = env_index;
    return constant;
}

static int vm_get_object_slot_ref(struct bc_constant *object, uint64_t slot_index, struct bc_constant ***slot_ref,
                                  char *error_buffer, size_t error_buffer_size) {
    if(object == NULL || object->kind != BC_CONST_OBJECT || object->value.object.slots == NULL) {
        vm_set_error(error_buffer, error_buffer_size, "object slot access requires an object value");
        return 0;
    }
    if(slot_index >= object->value.object.slot_count) {
        vm_set_error(error_buffer, error_buffer_size, "object slot index out of range");
        return 0;
    }

    *slot_ref = &object->value.object.slots[slot_index];
    return 1;
}

static struct bc_constant *vm_alloc_runtime_object(struct bc_vm *vm, uint64_t slot_count, char *error_buffer,
                                                   size_t error_buffer_size) {
    struct bc_constant *constant;

    if(vm->runtime_constant_count >= BC_VM_MAX_RUNTIME_CONSTANTS) {
        vm_set_error(error_buffer, error_buffer_size, "runtime constant storage exhausted");
        return NULL;
    }

    constant = &vm->runtime_constants[vm->runtime_constant_count++];
    constant->kind = BC_CONST_OBJECT;
    constant->value.object.slot_count = slot_count;
    constant->value.object.slots = NULL;

    if(slot_count > 0) {
        constant->value.object.slots = (struct bc_constant **)calloc((size_t)slot_count, sizeof(struct bc_constant *));
        if(constant->value.object.slots == NULL) {
            constant->kind = 0;
            constant->value.object.slot_count = 0;
            vm->runtime_constant_count--;
            vm_set_error(error_buffer, error_buffer_size, "runtime object allocation failed");
            return NULL;
        }
    }

    return constant;
}

static int vm_bind_object_environment(struct bc_vm *vm, struct bc_constant *object, size_t *env_index, char *error_buffer,
                                      size_t error_buffer_size) {
    struct bc_environment *environment;
    size_t parent_env_index = SIZE_MAX;

    if(object == NULL || object->kind != BC_CONST_OBJECT) {
        vm_set_error(error_buffer, error_buffer_size, "method binding requires an object receiver");
        return 0;
    }
    if(vm->environment_count >= BC_VM_MAX_ENVIRONMENTS) {
        vm_set_error(error_buffer, error_buffer_size, "environment storage exhausted");
        return 0;
    }

    if(object->value.object.slot_count > 1 && object->value.object.slots[1] != NULL
       && object->value.object.slots[1]->kind == BC_CONST_ENVREF) {
        parent_env_index = (size_t)object->value.object.slots[1]->value.env_index;
    }

    environment = &vm->environments[vm->environment_count];
    memset(environment, 0, sizeof(*environment));
    environment->parent_env_index = parent_env_index;
    environment->arg_count = 0;
    environment->local_count = (uint32_t)object->value.object.slot_count;
    environment->object = object;
    environment->self = object;
    *env_index = vm->environment_count++;
    return 1;
}

static int vm_bind_primitive_environment(struct bc_vm *vm, struct bc_constant *primitive,
                                         struct bc_constant *class_table, size_t *env_index,
                                         char *error_buffer, size_t error_buffer_size) {
    struct bc_environment *environment;
    struct bc_constant *wrapper;

    if(vm->environment_count >= BC_VM_MAX_ENVIRONMENTS) {
        vm_set_error(error_buffer, error_buffer_size, "environment storage exhausted");
        return 0;
    }

    /* Build a 2-slot wrapper: slot[0]=class_table, slot[1]=primitive value.
       LOAD_CAPTURE_LOCAL depth=1 slot=1 in primitive methods reads the receiver. */
    wrapper = vm_alloc_runtime_object(vm, 2, error_buffer, error_buffer_size);
    if(wrapper == NULL) { return 0; }
    wrapper->value.object.slots[0] = class_table;
    wrapper->value.object.slots[1] = primitive;

    /* Inherit the class table's construction context so depth>1 captures resolve correctly */
    size_t parent_env = SIZE_MAX;
    if(class_table != NULL && class_table->kind == BC_CONST_OBJECT
       && class_table->value.object.slot_count > 1 && class_table->value.object.slots[1] != NULL
       && class_table->value.object.slots[1]->kind == BC_CONST_ENVREF) {
        parent_env = (size_t)class_table->value.object.slots[1]->value.env_index;
    }

    environment = &vm->environments[vm->environment_count];
    memset(environment, 0, sizeof(*environment));
    environment->parent_env_index = parent_env;
    environment->arg_count = 0;
    environment->local_count = 0;
    environment->object = wrapper;
    environment->self = primitive;
    *env_index = vm->environment_count++;
    return 1;
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

        case 15: {
            int64_t n;
            if(argc != 1 || !vm_validate_integer_primitive_args(argv, 1, error_buffer, error_buffer_size)) { return 0; }
            n = argv[0]->value.integer;
            if(n < 0) {
                vm_set_error(error_buffer, error_buffer_size, "object_allocate: negative size");
                return 0;
            }
            result = vm_alloc_runtime_object(vm, (uint64_t)n, error_buffer, error_buffer_size);
            break;
        }

        case 16: {
            struct bc_constant **slot_ref;
            if(argc != 2) {
                vm_set_error(error_buffer, error_buffer_size, "object_at expects 2 arguments");
                return 0;
            }
            if(argv[1] == NULL || argv[1]->kind != BC_CONST_INTEGER) {
                vm_set_error(error_buffer, error_buffer_size, "object_at: index must be integer");
                return 0;
            }
            if(!vm_get_object_slot_ref(argv[0], (uint64_t)argv[1]->value.integer, &slot_ref, error_buffer,
                                       error_buffer_size)) {
                return 0;
            }
            vm->stack_size -= argc;
            vm->stack[vm->stack_size++] = *slot_ref;
            return 1;
        }

        case 17: {
            struct bc_constant **slot_ref;
            if(argc != 3) {
                vm_set_error(error_buffer, error_buffer_size, "object_atPut expects 3 arguments");
                return 0;
            }
            if(argv[1] == NULL || argv[1]->kind != BC_CONST_INTEGER) {
                vm_set_error(error_buffer, error_buffer_size, "object_atPut: index must be integer");
                return 0;
            }
            if(!vm_get_object_slot_ref(argv[0], (uint64_t)argv[1]->value.integer, &slot_ref, error_buffer,
                                       error_buffer_size)) {
                return 0;
            }
            *slot_ref = argv[2];
            result = vm_alloc_runtime_integer(vm, 0, error_buffer, error_buffer_size);
            break;
        }

        case 18:
            if(argc != 1) {
                vm_set_error(error_buffer, error_buffer_size, "object_cast expects 1 argument");
                return 0;
            }
            vm->stack_size -= argc;
            vm->stack[vm->stack_size++] = argv[0];
            return 1;

        case 19:
            if(argc != 1) {
                vm_set_error(error_buffer, error_buffer_size, "string_length expects 1 argument");
                return 0;
            }
            if(argv[0] == NULL || argv[0]->kind != BC_CONST_STRING) {
                vm_set_error(error_buffer, error_buffer_size, "string_length requires a string argument");
                return 0;
            }
            result = vm_alloc_runtime_integer(vm, (int64_t)strlen(argv[0]->value.string), error_buffer, error_buffer_size);
            break;

        case 20: {
            int64_t start, len, source_len;
            char *buffer;
            if(argc != 3) {
                vm_set_error(error_buffer, error_buffer_size, "string_substring expects 3 arguments");
                return 0;
            }
            if(argv[0] == NULL || argv[0]->kind != BC_CONST_STRING || argv[1] == NULL
               || argv[1]->kind != BC_CONST_INTEGER || argv[2] == NULL || argv[2]->kind != BC_CONST_INTEGER) {
                vm_set_error(error_buffer, error_buffer_size, "string_substring requires (string, integer, integer)");
                return 0;
            }
            start = argv[1]->value.integer;
            len = argv[2]->value.integer;
            fprintf(stderr, "DEBUG prim20: str=%s start=%lld len=%lld argc=%zu\n",
                    argv[0]->value.string, (long long)start, (long long)len, argc);
            source_len = (int64_t)strlen(argv[0]->value.string);
            if(start < 0) { start = 0; }
            if(start > source_len) { start = source_len; }
            if(len < 0) { len = 0; }
            if(start + len > source_len) { len = source_len - start; }
            buffer = (char *)malloc((size_t)len + 1);
            if(buffer == NULL) {
                vm_set_error(error_buffer, error_buffer_size, "string_substring allocation failed");
                return 0;
            }
            memcpy(buffer, argv[0]->value.string + start, (size_t)len);
            buffer[len] = '\0';
            result = vm_alloc_runtime_string(vm, buffer, error_buffer, error_buffer_size);
            free(buffer);
            break;
        }

        case 21: {
            char buffer[256];
            if(argc != 0) {
                vm_set_error(error_buffer, error_buffer_size, "stdin_read expects 0 arguments");
                return 0;
            }
            if(fgets(buffer, sizeof(buffer), stdin) == NULL) {
                vm->stack_size -= argc;
                vm->stack[vm->stack_size++] = NULL;
                return 1;
            }
            result = vm_alloc_runtime_string(vm, buffer, error_buffer, error_buffer_size);
            break;
        }

        case 22:
            if(argc != 1) {
                vm_set_error(error_buffer, error_buffer_size, "defined primitive expects one argument");
                return 0;
            }
            result = vm_alloc_runtime_boolean(vm, argv[0] != NULL, error_buffer, error_buffer_size);
            break;

        case 0:
            if(argc != 2) {
                vm_set_error(error_buffer, error_buffer_size, "object_equals expects 2 arguments");
                return 0;
            }
            result = vm_alloc_runtime_boolean(vm, argv[0] == argv[1], error_buffer, error_buffer_size);
            break;

        case 1: {
            if(argc != 2) {
                vm_set_error(error_buffer, error_buffer_size, "string_compare expects 2 arguments");
                return 0;
            }
            if(argv[0] == NULL || argv[0]->kind != BC_CONST_STRING || argv[1] == NULL
               || argv[1]->kind != BC_CONST_STRING) {
                vm_set_error(error_buffer, error_buffer_size, "string_compare requires string arguments");
                return 0;
            }
            int cmp = strcmp(argv[0]->value.string, argv[1]->value.string);
            result = vm_alloc_runtime_integer(vm, (int64_t)cmp, error_buffer, error_buffer_size);
            break;
        }

        case 2:
            if(argc != 1) {
                vm_set_error(error_buffer, error_buffer_size, "string_print expects 1 argument");
                return 0;
            }
            if(argv[0] == NULL || argv[0]->kind != BC_CONST_STRING) {
                vm_set_error(error_buffer, error_buffer_size, "string_print requires a string argument");
                return 0;
            }
            printf("%s", argv[0]->value.string);
            result = vm_alloc_runtime_integer(vm, 0, error_buffer, error_buffer_size);
            break;

        case 3: {
            if(argc != 2) {
                vm_set_error(error_buffer, error_buffer_size, "string_concat expects 2 arguments");
                return 0;
            }
            if(argv[0] == NULL || argv[0]->kind != BC_CONST_STRING || argv[1] == NULL
               || argv[1]->kind != BC_CONST_STRING) {
                vm_set_error(error_buffer, error_buffer_size, "string_concat requires string arguments");
                return 0;
            }
            size_t total_len = strlen(argv[0]->value.string) + strlen(argv[1]->value.string);
            char *buf = (char *)malloc(total_len + 1);
            if(buf == NULL) {
                vm_set_error(error_buffer, error_buffer_size, "string_concat allocation failed");
                return 0;
            }
            strcpy(buf, argv[0]->value.string);
            strcat(buf, argv[1]->value.string);
            result = vm_alloc_runtime_string(vm, buf, error_buffer, error_buffer_size);
            free(buf);
            break;
        }

        case 9: {
            char buf[32];
            if(argc != 1 || !vm_validate_integer_primitive_args(argv, 1, error_buffer, error_buffer_size)) { return 0; }
            snprintf(buf, sizeof(buf), "%" PRId64, argv[0]->value.integer);
            result = vm_alloc_runtime_string(vm, buf, error_buffer, error_buffer_size);
            break;
        }

        case 14:
            if(argc != 1 || !vm_validate_integer_primitive_args(argv, 1, error_buffer, error_buffer_size)) { return 0; }
            result = vm_alloc_runtime_real(vm, (double)argv[0]->value.integer, error_buffer, error_buffer_size);
            break;

        case 23: {
            char buf[40];
            double v;
            if(argc != 1 || !vm_extract_real_value(argv[0], &v, error_buffer, error_buffer_size)) { return 0; }
            snprintf(buf, sizeof(buf), "%g", v);
            result = vm_alloc_runtime_string(vm, buf, error_buffer, error_buffer_size);
            break;
        }

        case 24: {
            double a, b;
            if(argc != 2 || !vm_extract_real_value(argv[0], &a, error_buffer, error_buffer_size)
               || !vm_extract_real_value(argv[1], &b, error_buffer, error_buffer_size)) {
                return 0;
            }
            result = vm_alloc_runtime_real(vm, a + b, error_buffer, error_buffer_size);
            break;
        }

        case 25: {
            double a, b;
            if(argc != 2 || !vm_extract_real_value(argv[0], &a, error_buffer, error_buffer_size)
               || !vm_extract_real_value(argv[1], &b, error_buffer, error_buffer_size)) {
                return 0;
            }
            result = vm_alloc_runtime_real(vm, a - b, error_buffer, error_buffer_size);
            break;
        }

        case 26: {
            double a, b;
            if(argc != 2 || !vm_extract_real_value(argv[0], &a, error_buffer, error_buffer_size)
               || !vm_extract_real_value(argv[1], &b, error_buffer, error_buffer_size)) {
                return 0;
            }
            result = vm_alloc_runtime_real(vm, a * b, error_buffer, error_buffer_size);
            break;
        }

        case 27: {
            double a, b;
            if(argc != 2 || !vm_extract_real_value(argv[0], &a, error_buffer, error_buffer_size)
               || !vm_extract_real_value(argv[1], &b, error_buffer, error_buffer_size)) {
                return 0;
            }
            if(b == 0.0) {
                vm_set_error(error_buffer, error_buffer_size, "division by zero in real primitive");
                return 0;
            }
            result = vm_alloc_runtime_real(vm, a / b, error_buffer, error_buffer_size);
            break;
        }

        case 28: {
            double a, b;
            if(argc != 2 || !vm_extract_real_value(argv[0], &a, error_buffer, error_buffer_size)
               || !vm_extract_real_value(argv[1], &b, error_buffer, error_buffer_size)) {
                return 0;
            }
            result = vm_alloc_runtime_boolean(vm, a < b, error_buffer, error_buffer_size);
            break;
        }

        case 29: {
            double v;
            if(argc != 1 || !vm_extract_real_value(argv[0], &v, error_buffer, error_buffer_size)) { return 0; }
            result = vm_alloc_runtime_integer(vm, (int64_t)v, error_buffer, error_buffer_size);
            break;
        }

        case 30: {
            double a, b;
            if(argc != 2 || !vm_extract_real_value(argv[0], &a, error_buffer, error_buffer_size)
               || !vm_extract_real_value(argv[1], &b, error_buffer, error_buffer_size)) {
                return 0;
            }
            result = vm_alloc_runtime_boolean(vm, a == b, error_buffer, error_buffer_size);
            break;
        }

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
        char msg[128];
        snprintf(msg, sizeof(msg), "argument count (%zu) does not match arity (%u) for fn=%s",
                 arg_count, function->arity, function->name ? function->name : "?");
        vm_set_error(error_buffer, error_buffer_size, msg);
        return 0;
    }

    if(vm->stack_size < arg_count) {
        vm_set_error(error_buffer, error_buffer_size, "operand stack underflow while entering frame");
        return 0;
    }

    size_t natural_arg_base = vm->stack_size - arg_count;
    size_t safe_start = 0;

    /* Ensure callee slots don't overlap any currently-active caller slot */
    if(vm->frame_count > 0) {
        struct bc_frame *caller = &vm->frames[caller_frame_index];
        safe_start = caller->local_base + caller->local_count;
    }

    arg_base = natural_arg_base > safe_start ? natural_arg_base : safe_start;
    local_base = arg_base + arg_count;

    /* Method frames need local slot 1 for 'self'; ensure at least 2 local slots are reserved */
    uint32_t effective_local_count = function->local_count;
    if(closure_env_index != SIZE_MAX && closure_env_index < vm->environment_count
       && vm->environments[closure_env_index].self != NULL && effective_local_count < 2) {
        effective_local_count = 2;
    }

    if(local_base + effective_local_count > BC_VM_MAX_FRAME_SLOTS) {
        vm_set_error(error_buffer, error_buffer_size, "frame slot storage exhausted");
        return 0;
    }

    frame = &vm->frames[vm->frame_count];
    memset(frame, 0, sizeof(*frame));
    frame->function = function;
    frame->caller_frame_index = caller_frame_index;
    frame->closure_env_index = closure_env_index;
    frame->promoted_env_index = SIZE_MAX;
    frame->stack_base = (uint32_t)natural_arg_base;  /* restore stack here on return */
    frame->arg_base = arg_base;
    frame->local_base = local_base;
    frame->arg_count = (uint32_t)arg_count;
    frame->local_count = effective_local_count;

    /* Copy args from their natural stack positions into the safe frame slot area */
    for(index = 0; index < arg_count; ++index) {
        vm->frame_slots[arg_base + index] = vm->stack[natural_arg_base + index];
    }
    memset(&vm->frame_slots[local_base], 0, effective_local_count * sizeof(vm->frame_slots[0]));

    /* Pre-populate local slot 1 with 'self' for method frames */
    if(effective_local_count >= 2 && closure_env_index != SIZE_MAX && closure_env_index < vm->environment_count) {
        struct bc_constant *self_val = vm->environments[closure_env_index].self;
        if(self_val != NULL) { vm->frame_slots[local_base + 1] = self_val; }
    }

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

static int vm_condition_is_false(struct bc_vm *vm, struct bc_constant *condition, int *is_false,
                                  char *error_buffer, size_t error_buffer_size) {
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

        case BC_CONST_OBJECT:
            /* Leda boolean object: check if class table matches the False class */
            if(condition->value.object.slot_count > 0 && condition->value.object.slots != NULL) {
                struct bc_constant *class_table = condition->value.object.slots[0];
                *is_false = (class_table != NULL && class_table == vm->builtin_class_tables[BC_BUILTIN_FALSE]);
            } else {
                *is_false = 0;
            }
            return 1;

        default:
            vm_set_error(error_buffer, error_buffer_size, "branch condition must currently be a boolean-compatible value");
            return 0;
    }
}

int bc_vm_init(struct bc_vm *vm, struct bc_module *module) {
    if(module == NULL || module->function_count == 0 || module->entry_function >= module->function_count) { return 0; }

    vm->module = module;
    vm->stack_size = 0;
    memset(vm->builtin_class_tables, 0, sizeof(vm->builtin_class_tables));
    memset(vm->frame_slots, 0, sizeof(vm->frame_slots));
    memset(vm->env_slots, 0, sizeof(vm->env_slots));
    memset(vm->runtime_constants, 0, sizeof(vm->runtime_constants));
    vm->runtime_constant_count = 0;
    memset(vm->environments, 0, sizeof(vm->environments));
    vm->environment_count = 0;
    vm->env_slot_count = 0;
    memset(vm->frames, 0, sizeof(vm->frames));
    vm->frame_count = 0;
    vm->current_frame_index = 0;
    return vm_enter_frame(vm, &module->functions[module->entry_function], 0, 0, SIZE_MAX, NULL, 0);
}

void bc_vm_free(struct bc_vm *vm) {
    size_t index;

    if(vm == NULL) { return; }

    for(index = 0; index < vm->runtime_constant_count; ++index) {
        if(vm->runtime_constants[index].kind == BC_CONST_OBJECT) {
            free(vm->runtime_constants[index].value.object.slots);
            vm->runtime_constants[index].value.object.slots = NULL;
            vm->runtime_constants[index].value.object.slot_count = 0;
        } else if(vm->runtime_constants[index].kind == BC_CONST_STRING) {
            free(vm->runtime_constants[index].value.string);
            vm->runtime_constants[index].value.string = NULL;
        }
    }

    vm->runtime_constant_count = 0;
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
                if(!vm_condition_is_false(vm, condition, &is_false, error_buffer, error_buffer_size)) { return 0; }
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
                uint64_t context_depth;
                size_t parent_env_index;
                size_t caller_frame_index = vm->current_frame_index;
                if(!vm_read_u64le(vm, &function_index) || !vm_read_u64le(vm, &argument_count)
                   || !vm_read_u64le(vm, &context_depth)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed CALL operand");
                    return 0;
                }
                if(function_index >= vm->module->function_count) {
                    vm_set_error(error_buffer, error_buffer_size, "invalid function index in CALL");
                    return 0;
                }
                if(!vm_resolve_closure_env_index(vm, context_depth, &parent_env_index, error_buffer, error_buffer_size)) {
                    return 0;
                }
                if(!vm_enter_frame(vm, &vm->module->functions[function_index], (size_t)argument_count, caller_frame_index,
                                   parent_env_index, error_buffer, error_buffer_size)) {
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

            case BC_OP_MAKE_REF_LOCAL: {
                uint64_t local_index;
                struct bc_constant **slot_ref;
                struct bc_constant *reference;

                if(!vm_read_u64le(vm, &local_index)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed MAKE_REF_LOCAL operand");
                    return 0;
                }
                frame = vm_current_frame(vm);
                if(!vm_get_local_slot_ref(vm, frame, local_index, &slot_ref, error_buffer, error_buffer_size)) { return 0; }
                reference = vm_alloc_runtime_reference(vm, slot_ref, error_buffer, error_buffer_size);
                if(reference == NULL || vm->stack_size >= BC_VM_MAX_STACK) {
                    if(reference != NULL) {
                        vm_set_error(error_buffer, error_buffer_size, "operand stack overflow in MAKE_REF_LOCAL");
                    }
                    return 0;
                }
                vm->stack[vm->stack_size++] = reference;
                break;
            }

            case BC_OP_MAKE_REF_ARG: {
                uint64_t arg_index;
                struct bc_constant **slot_ref;
                struct bc_constant *reference;

                if(!vm_read_u64le(vm, &arg_index)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed MAKE_REF_ARG operand");
                    return 0;
                }
                frame = vm_current_frame(vm);
                if(!vm_get_arg_slot_ref(vm, frame, arg_index, &slot_ref, error_buffer, error_buffer_size)) { return 0; }
                reference = vm_alloc_runtime_reference(vm, slot_ref, error_buffer, error_buffer_size);
                if(reference == NULL || vm->stack_size >= BC_VM_MAX_STACK) {
                    if(reference != NULL) {
                        vm_set_error(error_buffer, error_buffer_size, "operand stack overflow in MAKE_REF_ARG");
                    }
                    return 0;
                }
                vm->stack[vm->stack_size++] = reference;
                break;
            }

            case BC_OP_MAKE_REF_CAPTURE_LOCAL: {
                uint64_t depth;
                uint64_t local_index;
                struct bc_constant **slot_ref;
                struct bc_constant *reference;

                if(!vm_read_u64le(vm, &depth) || !vm_read_u64le(vm, &local_index)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed MAKE_REF_CAPTURE_LOCAL operand");
                    return 0;
                }
                if(!vm_get_capture_local_slot_ref(vm, depth, local_index, &slot_ref, error_buffer, error_buffer_size)) {
                    return 0;
                }
                reference = vm_alloc_runtime_reference(vm, slot_ref, error_buffer, error_buffer_size);
                if(reference == NULL || vm->stack_size >= BC_VM_MAX_STACK) {
                    if(reference != NULL) {
                        vm_set_error(error_buffer, error_buffer_size, "operand stack overflow in MAKE_REF_CAPTURE_LOCAL");
                    }
                    return 0;
                }
                vm->stack[vm->stack_size++] = reference;
                break;
            }

            case BC_OP_MAKE_REF_CAPTURE_ARG: {
                uint64_t depth;
                uint64_t arg_index;
                struct bc_constant **slot_ref;
                struct bc_constant *reference;

                if(!vm_read_u64le(vm, &depth) || !vm_read_u64le(vm, &arg_index)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed MAKE_REF_CAPTURE_ARG operand");
                    return 0;
                }
                if(!vm_get_capture_arg_slot_ref(vm, depth, arg_index, &slot_ref, error_buffer, error_buffer_size)) { return 0; }
                reference = vm_alloc_runtime_reference(vm, slot_ref, error_buffer, error_buffer_size);
                if(reference == NULL || vm->stack_size >= BC_VM_MAX_STACK) {
                    if(reference != NULL) {
                        vm_set_error(error_buffer, error_buffer_size, "operand stack overflow in MAKE_REF_CAPTURE_ARG");
                    }
                    return 0;
                }
                vm->stack[vm->stack_size++] = reference;
                break;
            }

            case BC_OP_LOAD_REF: {
                struct bc_constant *reference;

                if(vm->stack_size == 0) {
                    vm_set_error(error_buffer, error_buffer_size, "operand stack underflow in LOAD_REF");
                    return 0;
                }

                reference = vm->stack[vm->stack_size - 1];
                if(reference == NULL || reference->kind != BC_CONST_REFERENCE || reference->value.slot_ref == NULL) {
                    vm_set_error(error_buffer, error_buffer_size, "LOAD_REF requires a reference operand");
                    return 0;
                }

                vm->stack[vm->stack_size - 1] = *reference->value.slot_ref;
                break;
            }

            case BC_OP_BUILD_INSTANCE: {
                uint64_t slot_count;
                uint64_t arg_count;
                struct bc_constant *instance;
                struct bc_constant *context_ref;
                size_t current_env_index;
                size_t table_index;
                size_t arg_index;

                if(!vm_read_u64le(vm, &slot_count) || !vm_read_u64le(vm, &arg_count)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed BUILD_INSTANCE operand");
                    return 0;
                }
                if(slot_count < 2 || arg_count + 2 > slot_count) {
                    vm_set_error(error_buffer, error_buffer_size, "invalid BUILD_INSTANCE layout");
                    return 0;
                }
                if(vm->stack_size < arg_count + 1) {
                    vm_set_error(error_buffer, error_buffer_size, "operand stack underflow in BUILD_INSTANCE");
                    return 0;
                }

                table_index = vm->stack_size - (size_t)arg_count - 1;
                instance = vm_alloc_runtime_object(vm, slot_count, error_buffer, error_buffer_size);
                if(instance == NULL) { return 0; }
                if(!vm_resolve_closure_env_index(vm, 0, &current_env_index, error_buffer, error_buffer_size)) { return 0; }
                context_ref = vm_alloc_runtime_envref(vm, (uint64_t)current_env_index, error_buffer, error_buffer_size);
                if(context_ref == NULL) { return 0; }

                instance->value.object.slots[0] = vm->stack[table_index];
                instance->value.object.slots[1] = context_ref;
                for(arg_index = 0; arg_index < (size_t)arg_count; ++arg_index) {
                    instance->value.object.slots[arg_index + 2] = vm->stack[table_index + 1 + arg_index];
                }

                vm->stack_size = table_index;
                if(vm->stack_size >= BC_VM_MAX_STACK) {
                    vm_set_error(error_buffer, error_buffer_size, "operand stack overflow after BUILD_INSTANCE");
                    return 0;
                }
                vm->stack[vm->stack_size++] = instance;
                break;
            }

            case BC_OP_LOAD_OBJECT_SLOT: {
                uint64_t slot_index;
                struct bc_constant **slot_ref;

                if(!vm_read_u64le(vm, &slot_index)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed LOAD_OBJECT_SLOT operand");
                    return 0;
                }
                if(vm->stack_size == 0) {
                    vm_set_error(error_buffer, error_buffer_size, "operand stack underflow in LOAD_OBJECT_SLOT");
                    return 0;
                }
                if(!vm_get_object_slot_ref(vm->stack[vm->stack_size - 1], slot_index, &slot_ref, error_buffer,
                                           error_buffer_size)) {
                    return 0;
                }
                vm->stack[vm->stack_size - 1] = *slot_ref;
                break;
            }

            case BC_OP_STORE_OBJECT_SLOT: {
                uint64_t slot_index;
                struct bc_constant **slot_ref;

                if(!vm_read_u64le(vm, &slot_index)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed STORE_OBJECT_SLOT operand");
                    return 0;
                }
                if(vm->stack_size < 2) {
                    vm_set_error(error_buffer, error_buffer_size, "operand stack underflow in STORE_OBJECT_SLOT");
                    return 0;
                }
                if(!vm_get_object_slot_ref(vm->stack[vm->stack_size - 1], slot_index, &slot_ref, error_buffer,
                                           error_buffer_size)) {
                    return 0;
                }
                *slot_ref = vm->stack[vm->stack_size - 2];
                vm->stack_size--;
                break;
            }

            case BC_OP_MAKE_REF_OBJECT_SLOT: {
                uint64_t slot_index;
                struct bc_constant **slot_ref;
                struct bc_constant *reference;

                if(!vm_read_u64le(vm, &slot_index)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed MAKE_REF_OBJECT_SLOT operand");
                    return 0;
                }
                if(vm->stack_size == 0) {
                    vm_set_error(error_buffer, error_buffer_size, "operand stack underflow in MAKE_REF_OBJECT_SLOT");
                    return 0;
                }
                if(!vm_get_object_slot_ref(vm->stack[vm->stack_size - 1], slot_index, &slot_ref, error_buffer,
                                           error_buffer_size)) {
                    return 0;
                }
                reference = vm_alloc_runtime_reference(vm, slot_ref, error_buffer, error_buffer_size);
                if(reference == NULL) { return 0; }
                vm->stack[vm->stack_size - 1] = reference;
                break;
            }

            case BC_OP_BR_IF_NOT_KIND: {
                int64_t delta;
                struct bc_constant *base;
                struct bc_constant *class_obj;
                struct bc_constant *current_class;
                bool matched = false;

                if(!vm_read_i64le(vm, &delta)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed BR_IF_NOT_KIND operand");
                    return 0;
                }
                if(vm->stack_size < 2) {
                    vm_set_error(error_buffer, error_buffer_size, "operand stack underflow in BR_IF_NOT_KIND");
                    return 0;
                }

                class_obj = vm->stack[--vm->stack_size];
                base = vm->stack[vm->stack_size - 1];

                if(base != NULL && base->kind == BC_CONST_OBJECT && base->value.object.slot_count > 0) {
                    current_class = base->value.object.slots[0];
                    while(current_class != NULL && current_class->kind == BC_CONST_OBJECT) {
                        if(current_class == class_obj) {
                            matched = true;
                            break;
                        }
                        if(current_class->value.object.slot_count <= 4) { break; }
                        struct bc_constant *parent = current_class->value.object.slots[4];
                        if(parent == NULL || parent->kind != BC_CONST_OBJECT || parent == current_class) { break; }
                        current_class = parent;
                    }
                }

                if(!matched) {
                    vm->stack_size--;
                    if(!vm_apply_jump_delta(vm, delta, error_buffer, error_buffer_size,
                                            "BR_IF_NOT_KIND jump target out of range")) {
                        return 0;
                    }
                }
                break;
            }

            case BC_OP_MAKE_METHOD: {
                uint64_t method_index;
                size_t method_env_index;
                struct bc_constant **method_ref;
                struct bc_constant *receiver;
                struct bc_constant *method_value;
                struct bc_constant *bound_method;
                struct bc_constant *class_table;

                if(!vm_read_u64le(vm, &method_index)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed MAKE_METHOD operand");
                    return 0;
                }
                if(vm->stack_size == 0) {
                    vm_set_error(error_buffer, error_buffer_size, "operand stack underflow in MAKE_METHOD");
                    return 0;
                }
                receiver = vm->stack[vm->stack_size - 1];

                if(receiver != NULL && receiver->kind == BC_CONST_OBJECT) {
                    if(!vm_bind_object_environment(vm, receiver, &method_env_index, error_buffer, error_buffer_size)) {
                        return 0;
                    }
                    if(!vm_get_object_slot_ref(receiver, 0, &method_ref, error_buffer, error_buffer_size)) { return 0; }
                    if(!vm_get_object_slot_ref(*method_ref, method_index, &method_ref, error_buffer, error_buffer_size)) {
                        return 0;
                    }
                } else {
                    size_t builtin_idx;
                    switch(receiver != NULL ? (int)receiver->kind : -1) {
                        case BC_CONST_INTEGER: builtin_idx = BC_BUILTIN_INTEGER; break;
                        case BC_CONST_STRING:  builtin_idx = BC_BUILTIN_STRING;  break;
                        case BC_CONST_BOOLEAN:
                            builtin_idx = receiver->value.integer ? BC_BUILTIN_TRUE : BC_BUILTIN_FALSE;
                            break;
                        case BC_CONST_REAL:    builtin_idx = BC_BUILTIN_REAL;    break;
                        default:
                            vm_set_error(error_buffer, error_buffer_size,
                                         "MAKE_METHOD requires an object or registered primitive receiver");
                            return 0;
                    }
                    class_table = vm->builtin_class_tables[builtin_idx];
                    if(class_table == NULL) {
                        vm_set_error(error_buffer, error_buffer_size,
                                     "builtin class table not yet registered for MAKE_METHOD");
                        return 0;
                    }
                    if(!vm_bind_primitive_environment(vm, receiver, class_table, &method_env_index, error_buffer,
                                                      error_buffer_size)) {
                        return 0;
                    }
                    if(!vm_get_object_slot_ref(class_table, method_index, &method_ref, error_buffer, error_buffer_size)) {
                        return 0;
                    }
                }

                method_value = *method_ref;
                if(method_value == NULL || method_value->kind != BC_CONST_FUNCTION) {
                    vm_set_error(error_buffer, error_buffer_size, "method table entry is not a function closure");
                    return 0;
                }
                bound_method = vm_alloc_runtime_function(vm, method_value->value.closure.function_index,
                                                         (uint64_t)method_env_index, error_buffer, error_buffer_size);
                if(bound_method == NULL) { return 0; }
                vm->stack[vm->stack_size - 1] = bound_method;
                break;
            }

            case BC_OP_REGISTER_BUILTIN: {
                uint64_t tag;
                if(!vm_read_u64le(vm, &tag)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed REGISTER_BUILTIN operand");
                    return 0;
                }
                if(tag >= BC_BUILTIN_COUNT) {
                    vm_set_error(error_buffer, error_buffer_size, "REGISTER_BUILTIN tag out of range");
                    return 0;
                }
                if(vm->stack_size == 0) {
                    vm_set_error(error_buffer, error_buffer_size, "operand stack underflow in REGISTER_BUILTIN");
                    return 0;
                }
                vm->builtin_class_tables[tag] = vm->stack[vm->stack_size - 1];
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