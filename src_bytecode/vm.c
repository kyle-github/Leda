#include "vm.h"

#include <stdio.h>
#include <string.h>

static void vm_set_error(char *error_buffer, size_t error_buffer_size, const char *message) {
    if(error_buffer != NULL && error_buffer_size > 0) { snprintf(error_buffer, error_buffer_size, "%s", message); }
}

static struct bc_frame *vm_current_frame(struct bc_vm *vm) { return &vm->frames[vm->current_frame_index]; }

static int vm_enter_frame(struct bc_vm *vm, struct bc_function *function, size_t arg_count, size_t caller_frame_index,
                          char *error_buffer, size_t error_buffer_size) {
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

static int vm_read_uleb128(struct bc_vm *vm, uint64_t *value) {
    struct bc_frame *frame = vm_current_frame(vm);
    uint64_t result = 0;
    int shift = 0;

    for(;;) {
        uint8_t byte;

        if(frame->ip >= frame->function->code.size) { return 0; }
        byte = frame->function->code.data[frame->ip++];
        result |= ((uint64_t)(byte & 0x7fu)) << shift;
        if((byte & 0x80u) == 0) {
            *value = result;
            return 1;
        }

        shift += 7;
        if(shift > 63) { return 0; }
    }
}

int bc_vm_init(struct bc_vm *vm, struct bc_module *module) {
    if(module == NULL || module->function_count == 0 || module->entry_function >= module->function_count) { return 0; }

    vm->module = module;
    vm->stack_size = 0;
    memset(vm->frame_slots, 0, sizeof(vm->frame_slots));
    memset(vm->frames, 0, sizeof(vm->frames));
    vm->frame_count = 0;
    vm->current_frame_index = 0;
    return vm_enter_frame(vm, &module->functions[module->entry_function], 0, 0, NULL, 0);
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

            case BC_OP_CONST: {
                uint64_t constant_index;
                if(!vm_read_uleb128(vm, &constant_index)) {
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
                if(!vm_read_uleb128(vm, &function_index) || !vm_read_uleb128(vm, &argument_count)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed CALL operand");
                    return 0;
                }
                if(function_index >= vm->module->function_count) {
                    vm_set_error(error_buffer, error_buffer_size, "invalid function index in CALL");
                    return 0;
                }
                if(!vm_enter_frame(vm, &vm->module->functions[function_index], (size_t)argument_count, caller_frame_index,
                                   error_buffer, error_buffer_size)) {
                    return 0;
                }
                break;
            }

            case BC_OP_LOAD_LOCAL: {
                uint64_t local_index;
                size_t slot_index;
                if(!vm_read_uleb128(vm, &local_index)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed LOAD_LOCAL operand");
                    return 0;
                }
                frame = vm_current_frame(vm);
                slot_index = frame->local_base + (size_t)local_index;
                if(local_index >= frame->local_count || slot_index >= BC_VM_MAX_FRAME_SLOTS
                   || vm->stack_size >= BC_VM_MAX_STACK) {
                    vm_set_error(error_buffer, error_buffer_size, "invalid local load");
                    return 0;
                }
                vm->stack[vm->stack_size++] = vm->frame_slots[slot_index];
                break;
            }

            case BC_OP_STORE_LOCAL: {
                uint64_t local_index;
                size_t slot_index;
                if(!vm_read_uleb128(vm, &local_index)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed STORE_LOCAL operand");
                    return 0;
                }
                frame = vm_current_frame(vm);
                slot_index = frame->local_base + (size_t)local_index;
                if(vm->stack_size == 0 || local_index >= frame->local_count || slot_index >= BC_VM_MAX_FRAME_SLOTS) {
                    vm_set_error(error_buffer, error_buffer_size, "invalid local store");
                    return 0;
                }
                vm->frame_slots[slot_index] = vm->stack[vm->stack_size - 1];
                break;
            }

            case BC_OP_LOAD_ARG: {
                uint64_t arg_index;
                size_t slot_index;
                if(!vm_read_uleb128(vm, &arg_index)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed LOAD_ARG operand");
                    return 0;
                }
                frame = vm_current_frame(vm);
                slot_index = frame->arg_base + (size_t)arg_index;
                if(arg_index >= frame->arg_count || slot_index >= BC_VM_MAX_FRAME_SLOTS || vm->stack_size >= BC_VM_MAX_STACK) {
                    vm_set_error(error_buffer, error_buffer_size, "invalid argument load");
                    return 0;
                }
                vm->stack[vm->stack_size++] = vm->frame_slots[slot_index];
                break;
            }

            case BC_OP_STORE_ARG: {
                uint64_t arg_index;
                size_t slot_index;
                if(!vm_read_uleb128(vm, &arg_index)) {
                    vm_set_error(error_buffer, error_buffer_size, "malformed STORE_ARG operand");
                    return 0;
                }
                frame = vm_current_frame(vm);
                slot_index = frame->arg_base + (size_t)arg_index;
                if(vm->stack_size == 0 || arg_index >= frame->arg_count || slot_index >= BC_VM_MAX_FRAME_SLOTS) {
                    vm_set_error(error_buffer, error_buffer_size, "invalid argument store");
                    return 0;
                }
                vm->frame_slots[slot_index] = vm->stack[vm->stack_size - 1];
                break;
            }

            default: vm_set_error(error_buffer, error_buffer_size, "opcode not implemented in VM yet"); return 0;
        }
    }
}