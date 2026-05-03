#include "bc_emit.h"

#include <stdio.h>
#include <string.h>

struct bc_pending_function {
    struct statementRecord *code;
    size_t index;
};

struct bc_compile_context {
    struct bc_module *module;
    struct bc_pending_function pending[256];
    size_t pending_count;
    size_t anonymous_function_count;
};

enum bc_slot_ref_kind {
    BC_SLOT_REF_NONE = 0,
    BC_SLOT_REF_LOCAL,
    BC_SLOT_REF_ARG,
    BC_SLOT_REF_CAPTURE_LOCAL,
    BC_SLOT_REF_CAPTURE_ARG,
};

struct bc_slot_ref {
    enum bc_slot_ref_kind kind;
    uint64_t depth;
    uint64_t slot;
};

struct bc_known_call_entry {
    struct bc_slot_ref ref;
    size_t function_index;
    uint64_t context_depth;
};

struct bc_call_state {
    struct bc_known_call_entry entries[256];
    size_t count;
};

static int bc_compile_expression(struct bc_compile_context *context, struct expressionRecord *expression,
                                 struct bc_function *function, struct bc_call_state *call_state, char *error_buffer,
                                 size_t error_buffer_size);

static int bc_compile_statement_range(struct bc_compile_context *context, struct statementRecord *statement,
                                      struct statementRecord *stop, struct bc_function *function,
                                      struct bc_call_state *call_state, int *falls_through, char *error_buffer,
                                      size_t error_buffer_size);

static void bc_set_error(char *error_buffer, size_t error_buffer_size, const char *message) {
    if(error_buffer != NULL && error_buffer_size > 0) { snprintf(error_buffer, error_buffer_size, "%s", message); }
}

static int bc_set_unsupported_statement_error(struct statementRecord *statement, char *error_buffer, size_t error_buffer_size) {
    if(error_buffer != NULL && error_buffer_size > 0) {
        snprintf(error_buffer, error_buffer_size, "%s:%d: unsupported statement type %d in first bytecode lowering slice",
                 statement->fileName ? statement->fileName : "<unknown>", statement->lineNumber, (int)statement->statementType);
    }
    return 0;
}

static int bc_set_unsupported_expression_error(struct expressionRecord *expression, char *error_buffer,
                                               size_t error_buffer_size) {
    if(error_buffer != NULL && error_buffer_size > 0) {
        snprintf(error_buffer, error_buffer_size, "unsupported expression operator %d in first bytecode lowering slice",
                 (int)expression->operator);
    }
    return 0;
}

static int bc_emit_const_index(struct bc_function *function, size_t constant_index, char *error_buffer, size_t error_buffer_size,
                               const char *message) {
    if(!bc_emit_opcode(function, BC_OP_CONST) || !bc_emit_u64le(function, (uint64_t)constant_index)) {
        bc_set_error(error_buffer, error_buffer_size, message);
        return 0;
    }

    if(function->max_stack < 1u) { function->max_stack = 1u; }
    return 1;
}

static uint32_t bc_arity_from_type(struct typeRecord *type) {
    if(type == NULL) { return 0; }
    if(type->ttyp == constantType || type->ttyp == unresolvedType) { return bc_arity_from_type(type->u.u.baseType); }
    if(type->ttyp == resolvedType) { return bc_arity_from_type(type->u.r.baseType); }
    if(type->ttyp == qualifiedType) { return bc_arity_from_type(type->u.q.baseType); }
    if(type->ttyp != functionType) { return 0; }
    return (uint32_t)length(type->u.f.argumentTypes);
}

static int bc_emit_slot_load(struct bc_function *function, enum bc_opcode opcode, uint64_t slot, char *error_buffer,
                             size_t error_buffer_size, const char *message) {
    if(!bc_emit_opcode(function, opcode) || !bc_emit_u64le(function, slot)) {
        bc_set_error(error_buffer, error_buffer_size, message);
        return 0;
    }
    if(function->max_stack < 1u) { function->max_stack = 1u; }
    return 1;
}

static int bc_emit_slot_store(struct bc_function *function, enum bc_opcode opcode, uint64_t slot, char *error_buffer,
                              size_t error_buffer_size, const char *message) {
    if(!bc_emit_opcode(function, opcode) || !bc_emit_u64le(function, slot)) {
        bc_set_error(error_buffer, error_buffer_size, message);
        return 0;
    }
    return 1;
}

static int bc_emit_capture_slot_load(struct bc_function *function, enum bc_opcode opcode, uint64_t depth, uint64_t slot,
                                     char *error_buffer, size_t error_buffer_size, const char *message) {
    if(!bc_emit_opcode(function, opcode) || !bc_emit_u64le(function, depth) || !bc_emit_u64le(function, slot)) {
        bc_set_error(error_buffer, error_buffer_size, message);
        return 0;
    }
    if(function->max_stack < 1u) { function->max_stack = 1u; }
    return 1;
}

static int bc_emit_capture_slot_store(struct bc_function *function, enum bc_opcode opcode, uint64_t depth, uint64_t slot,
                                      char *error_buffer, size_t error_buffer_size, const char *message) {
    if(!bc_emit_opcode(function, opcode) || !bc_emit_u64le(function, depth) || !bc_emit_u64le(function, slot)) {
        bc_set_error(error_buffer, error_buffer_size, message);
        return 0;
    }
    return 1;
}

static int bc_emit_jump_placeholder(struct bc_function *function, enum bc_opcode opcode, size_t *operand_offset,
                                    char *error_buffer, size_t error_buffer_size, const char *message) {
    if(!bc_emit_opcode(function, opcode)) {
        bc_set_error(error_buffer, error_buffer_size, message);
        return 0;
    }

    *operand_offset = function->code.size;
    if(!bc_emit_i64le(function, 0)) {
        bc_set_error(error_buffer, error_buffer_size, message);
        return 0;
    }

    return 1;
}

static int bc_patch_jump_target(struct bc_function *function, size_t operand_offset, size_t target_offset, char *error_buffer,
                                size_t error_buffer_size, const char *message) {
    int64_t relative_delta;
    size_t index;
    uint64_t raw_delta;

    if(function == NULL || operand_offset + 8 > function->code.size) {
        bc_set_error(error_buffer, error_buffer_size, message);
        return 0;
    }

    relative_delta = (int64_t)target_offset - (int64_t)(operand_offset + 8);
    memcpy(&raw_delta, &relative_delta, sizeof(raw_delta));
    for(index = 0; index < 8; ++index) {
        function->code.data[operand_offset + index] = (uint8_t)((raw_delta >> (index * 8)) & 0xffu);
    }

    return 1;
}

static int bc_emit_jump_to_offset(struct bc_function *function, enum bc_opcode opcode, size_t target_offset, char *error_buffer,
                                  size_t error_buffer_size, const char *message) {
    size_t operand_offset;

    if(!bc_emit_jump_placeholder(function, opcode, &operand_offset, error_buffer, error_buffer_size, message)) { return 0; }
    return bc_patch_jump_target(function, operand_offset, target_offset, error_buffer, error_buffer_size, message);
}

static int bc_resolve_context_depth(struct expressionRecord *context_expression, uint64_t *depth) {
    uint64_t resolved_depth = 0;

    if(depth == NULL) { return 0; }

    while(context_expression != NULL && context_expression->operator== getOffset && context_expression->u.o.location == 1) {
        resolved_depth++;
        context_expression = context_expression->u.o.base;
    }

    if(context_expression == NULL || context_expression->operator!= getCurrentContext) { return 0; }

    *depth = resolved_depth;
    return 1;
}

static int bc_match_context_local_slot(struct expressionRecord *expression, uint64_t *depth, uint64_t *slot) {
    if(expression == NULL || (expression->operator!= getOffset && expression->operator!= getGlobalOffset)
       || expression->u.o.location < 0) {
        return 0;
    }

    if(!bc_resolve_context_depth(expression->u.o.base, depth)) { return 0; }

    *slot = (uint64_t)expression->u.o.location;
    return 1;
}

static int bc_match_context_local_slot_core(struct expressionRecord *base, int location, uint64_t *depth, uint64_t *slot) {
    if(base == NULL || location < 0) { return 0; }
    if(!bc_resolve_context_depth(base, depth)) { return 0; }

    *slot = (uint64_t)location;
    return 1;
}

static int bc_match_function_local_slot_with_depth(struct expressionRecord *expression, uint64_t *depth, uint64_t *slot) {
    struct expressionRecord *base;

    if(expression == NULL || expression->operator!= getOffset || expression->u.o.location<0) { return 0; }
    base = expression->u.o.base;
    if(base == NULL || base->operator!= getOffset || base->u.o.location != 3) { return 0; }
    if(!bc_resolve_context_depth(base->u.o.base, depth)) { return 0; }

    *slot = (uint64_t)expression->u.o.location;
    return 1;
}

static int bc_match_function_local_slot_with_depth_core(struct expressionRecord *base, int location, uint64_t *depth,
                                                        uint64_t *slot) {
    if(base == NULL || location < 0) { return 0; }
    if(base->operator!= getOffset || base->u.o.location != 3) { return 0; }
    if(!bc_resolve_context_depth(base->u.o.base, depth)) { return 0; }

    *slot = (uint64_t)location;
    return 1;
}

static int bc_match_function_arg_slot_with_depth(struct expressionRecord *expression, uint64_t *depth, uint64_t *slot) {
    if(expression == NULL || expression->operator!= getOffset || expression->u.o.location<4) { return 0; }
    if(!bc_resolve_context_depth(expression->u.o.base, depth)) { return 0; }

    *slot = (uint64_t)(expression->u.o.location - 4);
    return 1;
}

static int bc_match_function_arg_slot_with_depth_core(struct expressionRecord *base, int location, uint64_t *depth,
                                                      uint64_t *slot) {
    if(base == NULL || location < 4) { return 0; }
    if(!bc_resolve_context_depth(base, depth)) { return 0; }

    *slot = (uint64_t)(location - 4);
    return 1;
}

static int bc_match_function_local_slot_core(struct expressionRecord *base, int location, uint64_t *slot) {
    if(base == NULL || location < 0) { return 0; }
    if(base->operator!= getOffset || base->u.o.location != 3 || base->u.o.base == NULL || base->u.o
           .base->operator!= getCurrentContext) {
        return 0;
    }

    *slot = (uint64_t)location;
    return 1;
}

static int bc_match_function_local_slot(struct expressionRecord *expression, uint64_t *slot) {
    if(expression == NULL) { return 0; }
    return bc_match_function_local_slot_core(expression->u.o.base, expression->u.o.location, slot);
}

static int bc_match_function_arg_slot_core(struct expressionRecord *base, int location, uint64_t *slot) {
    if(base == NULL || base->operator!= getCurrentContext || location<4) { return 0; }

    *slot = (uint64_t)(location - 4);
    return 1;
}

static int bc_match_current_function_arg_slot_core(struct bc_function *function, struct expressionRecord *base, int location,
                                                   uint64_t *slot) {
    uint64_t resolved_slot;

    if(!bc_match_function_arg_slot_core(base, location, &resolved_slot)) { return 0; }
    if(function == NULL || resolved_slot >= function->arity) { return 0; }

    *slot = resolved_slot;
    return 1;
}

static int bc_reference_target_parts(struct expressionRecord *target, enum instructions *operator,
                                     struct expressionRecord ** base, int *location) {
    struct expressionRecord *reference_expression;

    if(target == NULL || operator== NULL || base == NULL || location == NULL) { return 0; }

    if(target->operator== getOffset || target->operator== getGlobalOffset) {
        *operator= target->operator;
        *base = target->u.o.base;
        *location = target->u.o.location;
        return 1;
    }

    if(target->operator!= makeReference) { return 0; }

    reference_expression = target;
    if(target->u.o.base != NULL && (target->u.o.base->operator== getOffset || target->u.o.base->operator== getGlobalOffset)
       && target->u.o.location == target->u.o.base->u.o.location) {
        reference_expression = target->u.o.base;
    }

    if(reference_expression->operator== getOffset || reference_expression->operator== getGlobalOffset) {
        *operator= reference_expression->operator;
        *base = reference_expression->u.o.base;
        *location = reference_expression->u.o.location;
        return 1;
    }

    if(target->u.o.base == NULL || target->u.o.location < 0) { return 0; }

    *operator= getOffset;
    *base = target->u.o.base;
    *location = target->u.o.location;
    return 1;
}

static int bc_match_function_arg_slot(struct expressionRecord *expression, uint64_t *slot) {
    if(expression == NULL) { return 0; }
    return bc_match_function_arg_slot_core(expression->u.o.base, expression->u.o.location, slot);
}

static void bc_call_state_clear(struct bc_call_state *call_state) {
    if(call_state != NULL) { call_state->count = 0; }
}

static int bc_slot_ref_equal(const struct bc_slot_ref *left, const struct bc_slot_ref *right) {
    return left != NULL && right != NULL && left->kind == right->kind && left->depth == right->depth && left->slot == right->slot;
}

static size_t bc_call_state_find_index(const struct bc_call_state *call_state, const struct bc_slot_ref *ref) {
    size_t index;

    if(call_state == NULL || ref == NULL || ref->kind == BC_SLOT_REF_NONE) { return (size_t)-1; }

    for(index = 0; index < call_state->count; ++index) {
        if(bc_slot_ref_equal(&call_state->entries[index].ref, ref)) { return index; }
    }

    return (size_t)-1;
}

static void bc_call_state_forget(struct bc_call_state *call_state, const struct bc_slot_ref *ref) {
    size_t index;

    if(call_state == NULL || ref == NULL) { return; }
    index = bc_call_state_find_index(call_state, ref);
    if(index == (size_t)-1) { return; }

    call_state->entries[index] = call_state->entries[call_state->count - 1];
    call_state->count--;
}

static void bc_call_state_remember(struct bc_call_state *call_state, const struct bc_slot_ref *ref, size_t function_index,
                                   uint64_t context_depth) {
    size_t index;

    if(call_state == NULL || ref == NULL || ref->kind == BC_SLOT_REF_NONE) { return; }

    index = bc_call_state_find_index(call_state, ref);
    if(index == (size_t)-1) {
        if(call_state->count >= (sizeof(call_state->entries) / sizeof(call_state->entries[0]))) { return; }
        index = call_state->count++;
    }

    call_state->entries[index].ref = *ref;
    call_state->entries[index].function_index = function_index;
    call_state->entries[index].context_depth = context_depth;
}

static int bc_call_state_lookup(const struct bc_call_state *call_state, const struct bc_slot_ref *ref, size_t *function_index,
                                uint64_t *context_depth) {
    size_t index;

    index = bc_call_state_find_index(call_state, ref);
    if(index == (size_t)-1) { return 0; }

    *function_index = call_state->entries[index].function_index;
    *context_depth = call_state->entries[index].context_depth;
    return 1;
}

static int bc_resolve_slot_ref_from_expression(struct bc_function *function, struct expressionRecord *expression,
                                               struct bc_slot_ref *ref) {
    uint64_t depth;
    uint64_t slot;

    if(ref == NULL) { return 0; }

    ref->kind = BC_SLOT_REF_NONE;
    ref->depth = 0;
    ref->slot = 0;

    if(expression == NULL || (expression->operator!= getOffset && expression->operator!= getGlobalOffset)) { return 0; }

    if(bc_match_function_local_slot_with_depth(expression, &depth, &slot)) {
        ref->kind = depth == 0 ? BC_SLOT_REF_LOCAL : BC_SLOT_REF_CAPTURE_LOCAL;
        ref->depth = depth;
        ref->slot = slot;
        return 1;
    }

    if(expression->operator== getOffset && bc_match_function_arg_slot_with_depth(expression, &depth, &slot)) {
        if(depth != 0 || (function != NULL && slot < function->arity)) {
            ref->kind = depth == 0 ? BC_SLOT_REF_ARG : BC_SLOT_REF_CAPTURE_ARG;
            ref->depth = depth;
            ref->slot = slot;
            return 1;
        }
    }

    if(bc_match_context_local_slot(expression, &depth, &slot)) {
        ref->kind = depth == 0 ? BC_SLOT_REF_LOCAL : BC_SLOT_REF_CAPTURE_LOCAL;
        ref->depth = depth;
        ref->slot = slot;
        return 1;
    }

    if(bc_match_function_local_slot(expression, &slot)) {
        ref->kind = BC_SLOT_REF_LOCAL;
        ref->slot = slot;
        return 1;
    }

    if(expression->operator== getOffset && bc_match_current_function_arg_slot_core(function, expression->u.o.base,
                                                                                   expression->u.o.location, &slot)) {
        ref->kind = BC_SLOT_REF_ARG;
        ref->slot = slot;
        return 1;
    }

    if(expression->u.o.base != NULL && expression->u.o.base->operator== getCurrentContext && expression->u.o.location >= 0) {
        ref->kind = BC_SLOT_REF_LOCAL;
        ref->slot = (uint64_t)expression->u.o.location;
        return 1;
    }

    return 0;
}

static int bc_resolve_slot_ref_from_assignment_target(struct bc_function *function, struct expressionRecord *target,
                                                      struct bc_slot_ref *ref) {
    enum instructions target_operator;
    struct expressionRecord *target_base;
    int target_location;
    uint64_t depth;
    uint64_t slot;

    if(ref == NULL) { return 0; }

    ref->kind = BC_SLOT_REF_NONE;
    ref->depth = 0;
    ref->slot = 0;

    if(!bc_reference_target_parts(target, &target_operator, &target_base, &target_location)) { return 0; }

    if(bc_match_function_local_slot_with_depth_core(target_base, target_location, &depth, &slot)
       || bc_match_context_local_slot_core(target_base, target_location, &depth, &slot)) {
        ref->kind = depth == 0 ? BC_SLOT_REF_LOCAL : BC_SLOT_REF_CAPTURE_LOCAL;
        ref->depth = depth;
        ref->slot = slot;
        return 1;
    }

    if(target_operator == getOffset && bc_match_function_arg_slot_with_depth_core(target_base, target_location, &depth, &slot)) {
        if(depth != 0 || (function != NULL && slot < function->arity)) {
            ref->kind = depth == 0 ? BC_SLOT_REF_ARG : BC_SLOT_REF_CAPTURE_ARG;
            ref->depth = depth;
            ref->slot = slot;
            return 1;
        }
    }

    if(bc_match_function_local_slot_core(target_base, target_location, &slot)
       || ((target_base != NULL && target_base->operator== getCurrentContext && target_location >= 0)
           && (slot = (uint64_t)target_location, 1))) {
        ref->kind = BC_SLOT_REF_LOCAL;
        ref->slot = slot;
        return 1;
    }

    if(target_operator == getOffset && bc_match_current_function_arg_slot_core(function, target_base, target_location, &slot)) {
        ref->kind = BC_SLOT_REF_ARG;
        ref->slot = slot;
        return 1;
    }

    return 0;
}

static struct statementRecord *bc_find_linear_join(struct statementRecord *first, struct statementRecord *second) {
    struct statementRecord *visited[512];
    size_t visited_count = 0;
    struct statementRecord *cursor;

    for(cursor = first; cursor != NULL && visited_count < (sizeof(visited) / sizeof(visited[0])); cursor = cursor->next) {
        size_t index;

        for(index = 0; index < visited_count; ++index) {
            if(visited[index] == cursor) {
                cursor = NULL;
                break;
            }
        }
        if(cursor == NULL) { break; }
        visited[visited_count++] = cursor;
    }

    for(cursor = second; cursor != NULL; cursor = cursor->next) {
        size_t index;

        for(index = 0; index < visited_count; ++index) {
            if(visited[index] == cursor) { return cursor; }
        }
    }

    return NULL;
}

static int bc_statement_reaches_linear(struct statementRecord *start, struct statementRecord *target) {
    struct statementRecord *visited[512];
    size_t visited_count = 0;

    for(; start != NULL && visited_count < (sizeof(visited) / sizeof(visited[0])); start = start->next) {
        size_t index;

        if(start == target) { return 1; }
        for(index = 0; index < visited_count; ++index) {
            if(visited[index] == start) { return 0; }
        }
        visited[visited_count++] = start;
    }

    return 0;
}

static int bc_compile_control_flow_statement(struct bc_compile_context *context, struct statementRecord *statement,
                                             struct statementRecord *stop, struct bc_function *function,
                                             struct bc_call_state *call_state, struct statementRecord **next_statement,
                                             int *falls_through, char *error_buffer, size_t error_buffer_size) {
    struct statementRecord *join;
    size_t false_jump_operand_offset;

    if(statement == NULL || statement->statementType != conditionalStatement) {
        bc_set_error(error_buffer, error_buffer_size, "internal error: expected conditional statement");
        return 0;
    }

    if(bc_statement_reaches_linear(statement->next, statement)) {
        struct bc_call_state loop_state;
        size_t loop_start_offset = function->code.size;

        if(!bc_compile_expression(context, statement->u.c.expr, function, call_state, error_buffer, error_buffer_size)) {
            return 0;
        }
        if(!bc_emit_jump_placeholder(function, BC_OP_JUMP_IF_FALSE, &false_jump_operand_offset, error_buffer, error_buffer_size,
                                     "unable to emit while false jump")) {
            return 0;
        }
        loop_state = *call_state;
        if(!bc_compile_statement_range(context, statement->next, statement, function, &loop_state, falls_through, error_buffer,
                                       error_buffer_size)) {
            return 0;
        }
        if(*falls_through
           && !bc_emit_jump_to_offset(function, BC_OP_JUMP, loop_start_offset, error_buffer, error_buffer_size,
                                      "unable to emit while back edge")) {
            return 0;
        }
        if(!bc_patch_jump_target(function, false_jump_operand_offset, function->code.size, error_buffer, error_buffer_size,
                                 "unable to patch while false jump")) {
            return 0;
        }

        bc_call_state_clear(call_state);
        *falls_through = 1;
        *next_statement = statement->u.c.falsePart;
        return 1;
    }

    join = bc_find_linear_join(statement->next, statement->u.c.falsePart);
    if(join == NULL) {
        bc_set_error(error_buffer, error_buffer_size, "unsupported conditional control-flow shape");
        return 0;
    }

    if(!bc_compile_expression(context, statement->u.c.expr, function, call_state, error_buffer, error_buffer_size)) { return 0; }
    if(!bc_emit_jump_placeholder(function, BC_OP_JUMP_IF_FALSE, &false_jump_operand_offset, error_buffer, error_buffer_size,
                                 "unable to emit conditional false jump")) {
        return 0;
    }

    {
        int true_falls_through = 1;
        int false_falls_through = 1;
        int has_false_branch = statement->u.c.falsePart != join;
        size_t end_jump_operand_offset = 0;
        int needs_end_jump = 0;
        struct bc_call_state true_state = *call_state;
        struct bc_call_state false_state = *call_state;

        if(!bc_compile_statement_range(context, statement->next, join, function, &true_state, &true_falls_through, error_buffer,
                                       error_buffer_size)) {
            return 0;
        }

        if(has_false_branch && true_falls_through) {
            if(!bc_emit_jump_placeholder(function, BC_OP_JUMP, &end_jump_operand_offset, error_buffer, error_buffer_size,
                                         "unable to emit conditional join jump")) {
                return 0;
            }
            needs_end_jump = 1;
        }

        if(!bc_patch_jump_target(function, false_jump_operand_offset, function->code.size, error_buffer, error_buffer_size,
                                 "unable to patch conditional false jump")) {
            return 0;
        }

        if(has_false_branch) {
            if(!bc_compile_statement_range(context, statement->u.c.falsePart, join, function, &false_state, &false_falls_through,
                                           error_buffer, error_buffer_size)) {
                return 0;
            }
        }

        if(needs_end_jump
           && !bc_patch_jump_target(function, end_jump_operand_offset, function->code.size, error_buffer, error_buffer_size,
                                    "unable to patch conditional join jump")) {
            return 0;
        }

        *falls_through = true_falls_through || false_falls_through;
    }

    bc_call_state_clear(call_state);
    *next_statement = join;
    return 1;
}

static int bc_ensure_function_compiled(struct bc_compile_context *context, struct expressionRecord *closure_expression,
                                       size_t *function_index, char *error_buffer, size_t error_buffer_size) {
    char generated_name[64];
    const char *function_name;
    struct bc_function *function;
    int falls_through = 1;
    size_t pending_index;

    if(closure_expression == NULL || closure_expression->operator!= makeClosure || closure_expression->u.l.code == NULL) {
        bc_set_error(error_buffer, error_buffer_size, "only direct closure calls are supported in the current bytecode slice");
        return 0;
    }

    for(pending_index = 0; pending_index < context->pending_count; ++pending_index) {
        if(context->pending[pending_index].code == closure_expression->u.l.code) {
            *function_index = context->pending[pending_index].index;
            return 1;
        }
    }

    function_name = closure_expression->u.l.functionName;
    if(function_name == NULL || function_name[0] == '\0') {
        snprintf(generated_name, sizeof(generated_name), "lambda_%zu", context->anonymous_function_count++);
        function_name = generated_name;
    }

    *function_index =
        bc_add_function(context->module, function_name, bc_arity_from_type(closure_expression->resultType), 0, 0, 0);
    if(*function_index == (size_t)-1) {
        bc_set_error(error_buffer, error_buffer_size, "unable to allocate bytecode function");
        return 0;
    }

    if(context->pending_count >= (sizeof(context->pending) / sizeof(context->pending[0]))) {
        bc_set_error(error_buffer, error_buffer_size, "too many pending bytecode functions");
        return 0;
    }

    context->pending[context->pending_count].code = closure_expression->u.l.code;
    context->pending[context->pending_count].index = *function_index;
    context->pending_count++;

    function = &context->module->functions[*function_index];
    {
        struct bc_call_state call_state;

        bc_call_state_clear(&call_state);
        if(!bc_compile_statement_range(context, closure_expression->u.l.code, NULL, function, &call_state, &falls_through,
                                       error_buffer, error_buffer_size)) {
            return 0;
        }
    }

    if(falls_through && !bc_emit_opcode(function, BC_OP_RETURN)) {
        bc_set_error(error_buffer, error_buffer_size, "unable to emit implicit RETURN opcode");
        return 0;
    }

    return 1;
}

static int bc_emit_closure_literal(struct bc_compile_context *context, struct expressionRecord *closure_expression,
                                   struct bc_function *function, char *error_buffer, size_t error_buffer_size) {
    size_t function_index;
    uint64_t context_depth;

    if(!bc_ensure_function_compiled(context, closure_expression, &function_index, error_buffer, error_buffer_size)) { return 0; }
    if(!bc_resolve_context_depth(closure_expression->u.l.context, &context_depth)) {
        bc_set_error(error_buffer, error_buffer_size, "unsupported closure context shape in current bytecode slice");
        return 0;
    }

    if(!bc_emit_opcode(function, BC_OP_MAKE_CLOSURE) || !bc_emit_u64le(function, (uint64_t)function_index)
       || !bc_emit_u64le(function, context_depth)) {
        bc_set_error(error_buffer, error_buffer_size, "unable to emit closure literal");
        return 0;
    }
    if(function->max_stack < 1u) { function->max_stack = 1u; }
    return 1;
}

static int bc_match_direct_closure_literal_target(struct bc_compile_context *context, struct expressionRecord *callee_expression,
                                                  size_t *function_index, uint64_t *context_depth, char *error_buffer,
                                                  size_t error_buffer_size) {
    if(callee_expression == NULL || callee_expression->operator!= makeClosure || callee_expression->u.l.code == NULL) {
        return 0;
    }

    if(!bc_ensure_function_compiled(context, callee_expression, function_index, error_buffer, error_buffer_size)) { return -1; }
    if(!bc_resolve_context_depth(callee_expression->u.l.context, context_depth)) {
        bc_set_error(error_buffer, error_buffer_size, "unsupported direct-call closure context shape");
        return -1;
    }

    return 1;
}

static int bc_match_direct_call_target(struct bc_compile_context *context, struct bc_function *function,
                                       struct expressionRecord *callee_expression, const struct bc_call_state *call_state,
                                       size_t *function_index, uint64_t *context_depth, char *error_buffer,
                                       size_t error_buffer_size) {
    struct bc_slot_ref ref;
    int literal_status = bc_match_direct_closure_literal_target(context, callee_expression, function_index, context_depth,
                                                                error_buffer, error_buffer_size);

    if(literal_status != 0) { return literal_status; }
    if(!bc_resolve_slot_ref_from_expression(function, callee_expression, &ref)) { return 0; }
    if(bc_call_state_lookup(call_state, &ref, function_index, context_depth)) { return 1; }
    return 0;
}

static int bc_expression_produces_value(struct expressionRecord *expression) {
    if(expression == NULL) { return 0; }

    switch(expression->operator) {
        case assignment: return 0;
        case commaOp: return bc_expression_produces_value(expression->u.a.right);
        default: return 1;
    }
}

static int bc_emit_reference_creation(struct bc_function *function, const struct bc_slot_ref *ref, char *error_buffer,
                                      size_t error_buffer_size) {
    switch(ref->kind) {
        case BC_SLOT_REF_LOCAL:
            return bc_emit_slot_load(function, BC_OP_MAKE_REF_LOCAL, ref->slot, error_buffer, error_buffer_size,
                                     "unable to emit local reference");

        case BC_SLOT_REF_ARG:
            return bc_emit_slot_load(function, BC_OP_MAKE_REF_ARG, ref->slot, error_buffer, error_buffer_size,
                                     "unable to emit argument reference");

        case BC_SLOT_REF_CAPTURE_LOCAL:
            return bc_emit_capture_slot_load(function, BC_OP_MAKE_REF_CAPTURE_LOCAL, ref->depth, ref->slot, error_buffer,
                                             error_buffer_size, "unable to emit captured local reference");

        case BC_SLOT_REF_CAPTURE_ARG:
            return bc_emit_capture_slot_load(function, BC_OP_MAKE_REF_CAPTURE_ARG, ref->depth, ref->slot, error_buffer,
                                             error_buffer_size, "unable to emit captured argument reference");

        default:
            bc_set_error(error_buffer, error_buffer_size, "unsupported reference target in current bytecode slice");
            return 0;
    }
}

static int bc_update_known_call_state_after_assignment(struct bc_compile_context *context, struct bc_function *function,
                                                       struct bc_call_state *call_state, struct expressionRecord *target,
                                                       struct expressionRecord *right, char *error_buffer,
                                                       size_t error_buffer_size) {
    struct bc_slot_ref ref;
    size_t function_index;
    uint64_t context_depth;
    int literal_status;

    if(call_state == NULL || !bc_resolve_slot_ref_from_assignment_target(function, target, &ref)) { return 1; }

    literal_status =
        bc_match_direct_closure_literal_target(context, right, &function_index, &context_depth, error_buffer, error_buffer_size);
    if(literal_status < 0) { return 0; }
    if(literal_status > 0) {
        bc_call_state_remember(call_state, &ref, function_index, context_depth);
    } else {
        bc_call_state_forget(call_state, &ref);
    }

    return 1;
}

static int bc_compile_assignment_target(struct expressionRecord *target, struct bc_function *function, char *error_buffer,
                                        size_t error_buffer_size) {
    enum instructions target_operator;
    struct expressionRecord *target_base;
    int target_location;
    uint64_t depth;
    uint64_t slot;

    if(!bc_reference_target_parts(target, &target_operator, &target_base, &target_location)) {
        return bc_set_unsupported_expression_error(target, error_buffer, error_buffer_size);
    }

    if(bc_match_function_local_slot_with_depth_core(target_base, target_location, &depth, &slot)) {
        if(depth == 0) {
            return bc_emit_slot_store(function, BC_OP_STORE_LOCAL, slot, error_buffer, error_buffer_size,
                                      "unable to emit local store");
        }
        return bc_emit_capture_slot_store(function, BC_OP_STORE_CAPTURE_LOCAL, depth, slot, error_buffer, error_buffer_size,
                                          "unable to emit captured local store");
    }

    if(target_operator == getOffset && bc_match_function_arg_slot_with_depth_core(target_base, target_location, &depth, &slot)) {
        if(depth == 0) {
            if(slot < function->arity) {
                return bc_emit_slot_store(function, BC_OP_STORE_ARG, slot, error_buffer, error_buffer_size,
                                          "unable to emit argument store");
            }
        } else {
            return bc_emit_capture_slot_store(function, BC_OP_STORE_CAPTURE_ARG, depth, slot, error_buffer, error_buffer_size,
                                              "unable to emit captured argument store");
        }
    }

    if(bc_match_context_local_slot_core(target_base, target_location, &depth, &slot)) {
        if(depth == 0) {
            return bc_emit_slot_store(function, BC_OP_STORE_LOCAL, slot, error_buffer, error_buffer_size,
                                      "unable to emit local store");
        }
        return bc_emit_capture_slot_store(function, BC_OP_STORE_CAPTURE_LOCAL, depth, slot, error_buffer, error_buffer_size,
                                          "unable to emit captured context store");
    }

    if(bc_match_function_local_slot_core(target_base, target_location, &slot)
       || ((target_base != NULL && target_base->operator== getCurrentContext && target_location >= 0)
           && (slot = (uint64_t)target_location, 1))) {
        return bc_emit_slot_store(function, BC_OP_STORE_LOCAL, slot, error_buffer, error_buffer_size,
                                  "unable to emit local store");
    }

    if(target_operator == getOffset && bc_match_current_function_arg_slot_core(function, target_base, target_location, &slot)) {
        return bc_emit_slot_store(function, BC_OP_STORE_ARG, slot, error_buffer, error_buffer_size,
                                  "unable to emit argument store");
    }

    bc_set_error(error_buffer, error_buffer_size,
                 "only current-frame locals and by-value arguments are supported in assignment targets");
    return 0;
}

static int bc_assignment_target_is_object_slot(struct bc_function *function, struct expressionRecord *target,
                                               struct expressionRecord **base, int *location) {
    enum instructions target_operator;
    struct bc_slot_ref ref;

    if(bc_resolve_slot_ref_from_assignment_target(function, target, &ref)) { return 0; }
    if(!bc_reference_target_parts(target, &target_operator, base, location)) { return 0; }
    return target_operator == getOffset && *base != NULL && *location >= 0;
}

static int bc_compile_expression(struct bc_compile_context *context, struct expressionRecord *expression,
                                 struct bc_function *function, struct bc_call_state *call_state, char *error_buffer,
                                 size_t error_buffer_size) {
    size_t constant_index;
    uint64_t depth;
    uint64_t slot;

    if(expression == NULL) { return 1; }

    switch(expression->operator) {
        case getOffset:
        case getGlobalOffset:
            if(bc_match_function_local_slot_with_depth(expression, &depth, &slot)) {
                if(depth == 0) {
                    return bc_emit_slot_load(function, BC_OP_LOAD_LOCAL, slot, error_buffer, error_buffer_size,
                                             "unable to emit local load");
                }
                return bc_emit_capture_slot_load(function, BC_OP_LOAD_CAPTURE_LOCAL, depth, slot, error_buffer, error_buffer_size,
                                                 "unable to emit captured local load");
            }
            if(expression->operator== getOffset && bc_match_function_arg_slot_with_depth(expression, &depth, &slot)) {
                if(depth == 0) {
                    if(slot < function->arity) {
                        return bc_emit_slot_load(function, BC_OP_LOAD_ARG, slot, error_buffer, error_buffer_size,
                                                 "unable to emit argument load");
                    }
                } else {
                    return bc_emit_capture_slot_load(function, BC_OP_LOAD_CAPTURE_ARG, depth, slot, error_buffer,
                                                     error_buffer_size, "unable to emit captured argument load");
                }
            }
            if(bc_match_context_local_slot(expression, &depth, &slot)) {
                if(depth == 0) {
                    return bc_emit_slot_load(function, BC_OP_LOAD_LOCAL, slot, error_buffer, error_buffer_size,
                                             "unable to emit slot load");
                }
                return bc_emit_capture_slot_load(function, BC_OP_LOAD_CAPTURE_LOCAL, depth, slot, error_buffer, error_buffer_size,
                                                 "unable to emit captured context load");
            }
            if(bc_match_function_local_slot(expression, &slot)) {
                return bc_emit_slot_load(function, BC_OP_LOAD_LOCAL, slot, error_buffer, error_buffer_size,
                                         "unable to emit local load");
            }
            if(expression->operator== getOffset && bc_match_current_function_arg_slot_core(function, expression->u.o.base,
                                                                                           expression->u.o.location, &slot)) {
                return bc_emit_slot_load(function, BC_OP_LOAD_ARG, slot, error_buffer, error_buffer_size,
                                         "unable to emit argument load");
            }
            if(expression->u.o.base != NULL
               && expression->u.o.base->operator== getCurrentContext && expression->u.o.location >= 0) {
                return bc_emit_slot_load(function, BC_OP_LOAD_LOCAL, (uint64_t)expression->u.o.location, error_buffer,
                                         error_buffer_size, "unable to emit slot load");
            }
            if(expression->operator== getOffset && expression->u.o.base != NULL && expression->u.o.location >= 0) {
                if(!bc_compile_expression(context, expression->u.o.base, function, call_state, error_buffer, error_buffer_size)) {
                    return 0;
                }
                if(!bc_emit_opcode(function, BC_OP_LOAD_OBJECT_SLOT)
                   || !bc_emit_u64le(function, (uint64_t)expression->u.o.location)) {
                    bc_set_error(error_buffer, error_buffer_size, "unable to emit object slot load");
                    return 0;
                }
                return 1;
            }
            bc_set_error(error_buffer, error_buffer_size,
                         "only current-frame locals, current-frame arguments, and top-level slots are supported in loads");
            return 0;

        case genIntegerConstant:
            constant_index = bc_add_integer_constant(context->module, expression->u.i.value);
            if(constant_index == (size_t)-1) {
                bc_set_error(error_buffer, error_buffer_size, "unable to allocate integer constant");
                return 0;
            }
            return bc_emit_const_index(function, constant_index, error_buffer, error_buffer_size,
                                       "unable to emit integer constant");

        case genRealConstant:
            constant_index = bc_add_real_constant(context->module, expression->u.r.value);
            if(constant_index == (size_t)-1) {
                bc_set_error(error_buffer, error_buffer_size, "unable to allocate real constant");
                return 0;
            }
            return bc_emit_const_index(function, constant_index, error_buffer, error_buffer_size, "unable to emit real constant");

        case genStringConstant:
            constant_index = bc_add_string_constant(context->module, expression->u.s.value);
            if(constant_index == (size_t)-1) {
                bc_set_error(error_buffer, error_buffer_size, "unable to allocate string constant");
                return 0;
            }
            return bc_emit_const_index(function, constant_index, error_buffer, error_buffer_size,
                                       "unable to emit string constant");

        case assignment: {
            struct expressionRecord *object_base;
            int object_location;

            if(bc_assignment_target_is_object_slot(function, expression->u.a.left, &object_base, &object_location)) {
                if(!bc_compile_expression(context, expression->u.a.right, function, call_state, error_buffer,
                                          error_buffer_size)) {
                    return 0;
                }
                if(!bc_compile_expression(context, object_base, function, call_state, error_buffer, error_buffer_size)) {
                    return 0;
                }
                if(!bc_emit_opcode(function, BC_OP_STORE_OBJECT_SLOT) || !bc_emit_u64le(function, (uint64_t)object_location)) {
                    bc_set_error(error_buffer, error_buffer_size, "unable to emit object slot store");
                    return 0;
                }
            } else {
                if(!bc_compile_expression(context, expression->u.a.right, function, call_state, error_buffer,
                                          error_buffer_size)) {
                    return 0;
                }
                if(!bc_compile_assignment_target(expression->u.a.left, function, error_buffer, error_buffer_size)) { return 0; }
            }
        }
            if(!bc_emit_opcode(function, BC_OP_POP)) {
                bc_set_error(error_buffer, error_buffer_size, "unable to emit POP for assignment expression");
                return 0;
            }
            if(!bc_update_known_call_state_after_assignment(context, function, call_state, expression->u.a.left,
                                                            expression->u.a.right, error_buffer, error_buffer_size)) {
                return 0;
            }
            return 1;

        case makeClosure:
            if(expression->u.l.code == NULL) {
                bc_set_error(error_buffer, error_buffer_size, "closure expression has no body");
                return 0;
            }
            return bc_emit_closure_literal(context, expression, function, error_buffer, error_buffer_size);

        case makeReference: {
            struct bc_slot_ref ref;
            struct expressionRecord *object_base;
            int object_location;

            if(!bc_resolve_slot_ref_from_assignment_target(function, expression, &ref)) {
                if(!bc_assignment_target_is_object_slot(function, expression, &object_base, &object_location)) {
                    bc_set_error(error_buffer, error_buffer_size,
                                 "only slot references are supported in makeReference bytecode lowering");
                    return 0;
                }
                if(!bc_compile_expression(context, object_base, function, call_state, error_buffer, error_buffer_size)) {
                    return 0;
                }
                if(!bc_emit_opcode(function, BC_OP_MAKE_REF_OBJECT_SLOT) || !bc_emit_u64le(function, (uint64_t)object_location)) {
                    bc_set_error(error_buffer, error_buffer_size, "unable to emit object slot reference");
                    return 0;
                }
                return 1;
            }
            return bc_emit_reference_creation(function, &ref, error_buffer, error_buffer_size);
        }

        case evalThunk:
            if(!bc_compile_expression(context, expression->u.o.base, function, call_state, error_buffer, error_buffer_size)) {
                return 0;
            }
            if(!bc_emit_opcode(function, BC_OP_CALL_CLOSURE) || !bc_emit_u64le(function, 0)) {
                bc_set_error(error_buffer, error_buffer_size, "unable to emit thunk evaluation");
                return 0;
            }
            if(function->max_stack < 2u) { function->max_stack = 2u; }
            return 1;

        case evalReference:
            if(!bc_compile_expression(context, expression->u.o.base, function, call_state, error_buffer, error_buffer_size)) {
                return 0;
            }
            if(!bc_emit_opcode(function, BC_OP_LOAD_REF)) {
                bc_set_error(error_buffer, error_buffer_size, "unable to emit reference load");
                return 0;
            }
            return 1;

        case commaOp:
            if(!bc_compile_expression(context, expression->u.a.left, function, call_state, error_buffer, error_buffer_size)) {
                return 0;
            }
            if(bc_expression_produces_value(expression->u.a.left) && !bc_emit_opcode(function, BC_OP_POP)) {
                bc_set_error(error_buffer, error_buffer_size, "unable to emit POP for comma left expression");
                return 0;
            }
            return bc_compile_expression(context, expression->u.a.right, function, call_state, error_buffer, error_buffer_size);

        case buildInstance: {
            uint64_t argument_count = 0;
            struct list *arg;

            if(expression->u.n.size < 2) {
                bc_set_error(error_buffer, error_buffer_size, "buildInstance requires at least method-table and global slots");
                return 0;
            }
            if(!bc_compile_expression(context, expression->u.n.table, function, call_state, error_buffer, error_buffer_size)) {
                return 0;
            }
            for(arg = expression->u.n.args; arg != NULL; arg = arg->next) {
                if(!bc_compile_expression(context, (struct expressionRecord *)arg->value, function, call_state, error_buffer,
                                          error_buffer_size)) {
                    return 0;
                }
                argument_count++;
            }
            if(argument_count + 2u > (uint64_t)expression->u.n.size) {
                bc_set_error(error_buffer, error_buffer_size, "too many constructor arguments for buildInstance layout");
                return 0;
            }
            if(!bc_emit_opcode(function, BC_OP_BUILD_INSTANCE) || !bc_emit_u64le(function, (uint64_t)expression->u.n.size)
               || !bc_emit_u64le(function, argument_count)) {
                bc_set_error(error_buffer, error_buffer_size, "unable to emit build instance opcode");
                return 0;
            }
            if(function->max_stack < argument_count + 1u) { function->max_stack = (uint32_t)(argument_count + 1u); }
            return 1;
        }

        case makeMethodContext:
            if(expression->u.o.base == NULL || expression->u.o.location < 0) {
                bc_set_error(error_buffer, error_buffer_size, "makeMethodContext requires a receiver and method slot");
                return 0;
            }
            if(!bc_compile_expression(context, expression->u.o.base, function, call_state, error_buffer, error_buffer_size)) {
                return 0;
            }
            if(!bc_emit_opcode(function, BC_OP_MAKE_METHOD) || !bc_emit_u64le(function, (uint64_t)expression->u.o.location)) {
                bc_set_error(error_buffer, error_buffer_size, "unable to emit method binding opcode");
                return 0;
            }
            return 1;

        case doFunctionCall: {
            size_t direct_function_index = 0;
            uint64_t direct_context_depth = 0;
            uint64_t argument_count = 0;
            struct list *arg;
            int direct_call_status =
                bc_match_direct_call_target(context, function, expression->u.f.fun, call_state, &direct_function_index,
                                            &direct_context_depth, error_buffer, error_buffer_size);

            if(direct_call_status < 0) { return 0; }

            if(direct_call_status == 0) {
                if(!bc_compile_expression(context, expression->u.f.fun, function, call_state, error_buffer, error_buffer_size)) {
                    return 0;
                }
            }

            for(arg = expression->u.f.args; arg != NULL; arg = arg->next) {
                if(!bc_compile_expression(context, (struct expressionRecord *)arg->value, function, call_state, error_buffer,
                                          error_buffer_size)) {
                    return 0;
                }
                argument_count++;
            }

            if(direct_call_status > 0) {
                if(!bc_emit_opcode(function, BC_OP_CALL) || !bc_emit_u64le(function, (uint64_t)direct_function_index)
                   || !bc_emit_u64le(function, argument_count) || !bc_emit_u64le(function, direct_context_depth)) {
                    bc_set_error(error_buffer, error_buffer_size, "unable to emit direct call");
                    return 0;
                }
                if(function->max_stack < argument_count + 1u) { function->max_stack = (uint32_t)(argument_count + 1u); }
                return 1;
            }

            if(!bc_emit_opcode(function, BC_OP_CALL_CLOSURE) || !bc_emit_u64le(function, argument_count)) {
                bc_set_error(error_buffer, error_buffer_size, "unable to emit indirect closure call");
                return 0;
            }
            if(function->max_stack < argument_count + 2u) { function->max_stack = (uint32_t)(argument_count + 2u); }
            return 1;
        }

        case doSpecialCall: {
            uint64_t argument_count = 0;
            struct list *arg;

            if(expression->u.c.index < 0) {
                bc_set_error(error_buffer, error_buffer_size, "primitive call has invalid primitive index");
                return 0;
            }

            for(arg = expression->u.c.args; arg != NULL; arg = arg->next) {
                if(!bc_compile_expression(context, (struct expressionRecord *)arg->value, function, call_state, error_buffer,
                                          error_buffer_size)) {
                    return 0;
                }
                argument_count++;
            }

            if(!bc_emit_opcode(function, BC_OP_CALL_PRIMITIVE) || !bc_emit_u64le(function, (uint64_t)expression->u.c.index)
               || !bc_emit_u64le(function, argument_count)) {
                bc_set_error(error_buffer, error_buffer_size, "unable to emit primitive call");
                return 0;
            }
            if(function->max_stack < argument_count + 1u) { function->max_stack = (uint32_t)(argument_count + 1u); }
            return 1;
        }

        default: return bc_set_unsupported_expression_error(expression, error_buffer, error_buffer_size);
    }
}

static int bc_compile_simple_statement(struct bc_compile_context *context, struct statementRecord *statement,
                                       struct bc_function *function, struct bc_call_state *call_state, char *error_buffer,
                                       size_t error_buffer_size) {
    switch(statement->statementType) {
        case makeLocalsStatement:
            if(statement->u.k.size > (int)function->local_count) { function->local_count = (uint32_t)statement->u.k.size; }
            return 1;

        case nullStatement: return 1;

        case expressionStatement:
            if(!bc_compile_expression(context, statement->u.r.e, function, call_state, error_buffer, error_buffer_size)) {
                return 0;
            }
            return 1;

        case returnStatement:
        case tailCall:
            if(!bc_compile_expression(context, statement->u.r.e, function, call_state, error_buffer, error_buffer_size)) {
                return 0;
            }
            if(!bc_emit_opcode(function, BC_OP_RETURN)) {
                bc_set_error(error_buffer, error_buffer_size, "unable to emit RETURN opcode");
                return 0;
            }
            return 1;

        default: return bc_set_unsupported_statement_error(statement, error_buffer, error_buffer_size);
    }
}

static int bc_compile_statement_range(struct bc_compile_context *context, struct statementRecord *statement,
                                      struct statementRecord *stop, struct bc_function *function,
                                      struct bc_call_state *call_state, int *falls_through, char *error_buffer,
                                      size_t error_buffer_size) {
    *falls_through = 1;

    for(; statement != NULL && statement != stop; statement = statement->next) {
        if(statement->statementType == conditionalStatement) {
            if(!bc_compile_control_flow_statement(context, statement, stop, function, call_state, &statement, falls_through,
                                                  error_buffer, error_buffer_size)) {
                return 0;
            }
            if(!*falls_through) { return 1; }
            if(statement == NULL || statement == stop) { break; }
            continue;
        }

        if(!bc_compile_simple_statement(context, statement, function, call_state, error_buffer, error_buffer_size)) { return 0; }
        if(statement->statementType == returnStatement) {
            *falls_through = 0;
            return 1;
        }
    }

    *falls_through = 1;
    return 1;
}

int bc_compile_top_level(struct symbolTableRecord *symbols, struct statementRecord *first_statement, struct bc_module *module,
                         char *error_buffer, size_t error_buffer_size) {
    struct bc_compile_context context;
    struct bc_call_state call_state;
    size_t function_index;
    struct bc_function *function;
    int falls_through = 1;

    (void)symbols;

    bc_module_init(module);
    memset(&context, 0, sizeof(context));
    context.module = module;

    if(!bc_reserve_function_capacity(module, 257)) {
        bc_set_error(error_buffer, error_buffer_size, "unable to reserve bytecode function capacity");
        bc_module_free(module);
        return 0;
    }

    function_index = bc_add_function(module, "__top__", 0, 0, 0, 0);
    if(function_index == (size_t)-1) {
        bc_set_error(error_buffer, error_buffer_size, "unable to allocate bytecode entry function");
        bc_module_free(module);
        return 0;
    }

    module->entry_function = (uint32_t)function_index;
    function = &module->functions[function_index];

    bc_call_state_clear(&call_state);
    if(!bc_compile_statement_range(&context, first_statement, NULL, function, &call_state, &falls_through, error_buffer,
                                   error_buffer_size)) {
        bc_module_free(module);
        return 0;
    }

    if(falls_through && !bc_emit_opcode(function, BC_OP_HALT)) {
        bc_set_error(error_buffer, error_buffer_size, "unable to emit halt opcode");
        bc_module_free(module);
        return 0;
    }

    bc_set_error(
        error_buffer, error_buffer_size,
        "compiled bytecode slice: top-level slots, function locals, captured outer slots, by-value arguments, closure values, direct and indirect calls, integer primitives, and local assignments");
    return 1;
}