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

static int bc_compile_expression(struct bc_compile_context *context, struct expressionRecord *expression,
                                 struct bc_function *function, char *error_buffer, size_t error_buffer_size);

static int bc_compile_statement_range(struct bc_compile_context *context, struct statementRecord *statement,
                                      struct statementRecord *stop, struct bc_function *function, int *falls_through,
                                      char *error_buffer, size_t error_buffer_size);

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

    if(target == NULL || target->operator!= makeReference || operator== NULL || base == NULL || location == NULL) { return 0; }

    reference_expression = target;
    if(target->u.o.base != NULL && (target->u.o.base->operator== getOffset || target->u.o.base->operator== getGlobalOffset)) {
        reference_expression = target->u.o.base;
    }

    if(reference_expression->operator!= getOffset && reference_expression->operator!= getGlobalOffset) { return 0; }

    *operator= reference_expression->operator;
    *base = reference_expression->u.o.base;
    *location = reference_expression->u.o.location;
    return 1;
}

static int bc_match_function_arg_slot(struct expressionRecord *expression, uint64_t *slot) {
    if(expression == NULL) { return 0; }
    return bc_match_function_arg_slot_core(expression->u.o.base, expression->u.o.location, slot);
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
                                             struct statementRecord **next_statement, int *falls_through, char *error_buffer,
                                             size_t error_buffer_size) {
    struct statementRecord *join;
    size_t false_jump_operand_offset;

    if(statement == NULL || statement->statementType != conditionalStatement) {
        bc_set_error(error_buffer, error_buffer_size, "internal error: expected conditional statement");
        return 0;
    }

    if(bc_statement_reaches_linear(statement->next, statement)) {
        size_t loop_start_offset = function->code.size;

        if(!bc_compile_expression(context, statement->u.c.expr, function, error_buffer, error_buffer_size)) { return 0; }
        if(!bc_emit_jump_placeholder(function, BC_OP_JUMP_IF_FALSE, &false_jump_operand_offset, error_buffer, error_buffer_size,
                                     "unable to emit while false jump")) {
            return 0;
        }
        if(!bc_compile_statement_range(context, statement->next, statement, function, falls_through, error_buffer,
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

        *falls_through = 1;
        *next_statement = statement->u.c.falsePart;
        return 1;
    }

    join = bc_find_linear_join(statement->next, statement->u.c.falsePart);
    if(join == NULL) {
        bc_set_error(error_buffer, error_buffer_size, "unsupported conditional control-flow shape");
        return 0;
    }

    if(!bc_compile_expression(context, statement->u.c.expr, function, error_buffer, error_buffer_size)) { return 0; }
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

        if(!bc_compile_statement_range(context, statement->next, join, function, &true_falls_through, error_buffer,
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
            if(!bc_compile_statement_range(context, statement->u.c.falsePart, join, function, &false_falls_through, error_buffer,
                                           error_buffer_size)) {
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
    if(!bc_compile_statement_range(context, closure_expression->u.l.code, NULL, function, &falls_through, error_buffer,
                                   error_buffer_size)) {
        return 0;
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
            if(slot >= function->arity) {
                bc_set_error(error_buffer, error_buffer_size,
                             "unsupported assignment target outside current function argument range");
                return 0;
            }
            return bc_emit_slot_store(function, BC_OP_STORE_ARG, slot, error_buffer, error_buffer_size,
                                      "unable to emit argument store");
        }
        return bc_emit_capture_slot_store(function, BC_OP_STORE_CAPTURE_ARG, depth, slot, error_buffer, error_buffer_size,
                                          "unable to emit captured argument store");
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

static int bc_compile_expression(struct bc_compile_context *context, struct expressionRecord *expression,
                                 struct bc_function *function, char *error_buffer, size_t error_buffer_size) {
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
                    return bc_emit_slot_load(function, BC_OP_LOAD_ARG, slot, error_buffer, error_buffer_size,
                                             "unable to emit argument load");
                }
                return bc_emit_capture_slot_load(function, BC_OP_LOAD_CAPTURE_ARG, depth, slot, error_buffer, error_buffer_size,
                                                 "unable to emit captured argument load");
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

        case assignment:
            if(!bc_compile_expression(context, expression->u.a.right, function, error_buffer, error_buffer_size)) { return 0; }
            if(!bc_compile_assignment_target(expression->u.a.left, function, error_buffer, error_buffer_size)) { return 0; }
            if(!bc_emit_opcode(function, BC_OP_POP)) {
                bc_set_error(error_buffer, error_buffer_size, "unable to emit POP for assignment expression");
                return 0;
            }
            return 1;

        case makeClosure:
            if(expression->u.l.code == NULL) {
                bc_set_error(error_buffer, error_buffer_size, "closure expression has no body");
                return 0;
            }
            return bc_emit_closure_literal(context, expression, function, error_buffer, error_buffer_size);

        case doFunctionCall: {
            uint64_t argument_count = 0;
            struct list *arg;

            if(!bc_compile_expression(context, expression->u.f.fun, function, error_buffer, error_buffer_size)) { return 0; }
            for(arg = expression->u.f.args; arg != NULL; arg = arg->next) {
                if(!bc_compile_expression(context, (struct expressionRecord *)arg->value, function, error_buffer,
                                          error_buffer_size)) {
                    return 0;
                }
                argument_count++;
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
                if(!bc_compile_expression(context, (struct expressionRecord *)arg->value, function, error_buffer,
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
                                       struct bc_function *function, char *error_buffer, size_t error_buffer_size) {
    switch(statement->statementType) {
        case makeLocalsStatement:
            if(statement->u.k.size > (int)function->local_count) { function->local_count = (uint32_t)statement->u.k.size; }
            return 1;

        case nullStatement: return 1;

        case expressionStatement:
            if(!bc_compile_expression(context, statement->u.r.e, function, error_buffer, error_buffer_size)) { return 0; }
            return 1;

        case returnStatement:
            if(!bc_compile_expression(context, statement->u.r.e, function, error_buffer, error_buffer_size)) { return 0; }
            if(!bc_emit_opcode(function, BC_OP_RETURN)) {
                bc_set_error(error_buffer, error_buffer_size, "unable to emit RETURN opcode");
                return 0;
            }
            return 1;

        default: return bc_set_unsupported_statement_error(statement, error_buffer, error_buffer_size);
    }
}

static int bc_compile_statement_range(struct bc_compile_context *context, struct statementRecord *statement,
                                      struct statementRecord *stop, struct bc_function *function, int *falls_through,
                                      char *error_buffer, size_t error_buffer_size) {
    *falls_through = 1;

    for(; statement != NULL && statement != stop; statement = statement->next) {
        if(statement->statementType == conditionalStatement) {
            if(!bc_compile_control_flow_statement(context, statement, stop, function, &statement, falls_through, error_buffer,
                                                  error_buffer_size)) {
                return 0;
            }
            if(!*falls_through) { return 1; }
            if(statement == NULL || statement == stop) { break; }
            continue;
        }

        if(!bc_compile_simple_statement(context, statement, function, error_buffer, error_buffer_size)) { return 0; }
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
    size_t function_index;
    struct bc_function *function;
    int falls_through = 1;

    (void)symbols;

    bc_module_init(module);
    memset(&context, 0, sizeof(context));
    context.module = module;

    function_index = bc_add_function(module, "__top__", 0, 0, 0, 0);
    if(function_index == (size_t)-1) {
        bc_set_error(error_buffer, error_buffer_size, "unable to allocate bytecode entry function");
        bc_module_free(module);
        return 0;
    }

    module->entry_function = (uint32_t)function_index;
    function = &module->functions[function_index];

    if(!bc_compile_statement_range(&context, first_statement, NULL, function, &falls_through, error_buffer, error_buffer_size)) {
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
        "compiled bytecode slice: top-level slots, function locals, captured outer slots, by-value arguments, closure values, indirect calls, integer primitives, and local assignments");
    return 1;
}