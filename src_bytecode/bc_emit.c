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

static int bc_compile_statement(struct bc_compile_context *context, struct statementRecord *statement,
                                struct bc_function *function, char *error_buffer, size_t error_buffer_size);

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
    if(!bc_emit_opcode(function, BC_OP_CONST) || !bc_emit_uleb128(function, (uint64_t)constant_index)) {
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
    if(!bc_emit_opcode(function, opcode) || !bc_emit_uleb128(function, slot)) {
        bc_set_error(error_buffer, error_buffer_size, message);
        return 0;
    }
    if(function->max_stack < 1u) { function->max_stack = 1u; }
    return 1;
}

static int bc_emit_slot_store(struct bc_function *function, enum bc_opcode opcode, uint64_t slot, char *error_buffer,
                              size_t error_buffer_size, const char *message) {
    if(!bc_emit_opcode(function, opcode) || !bc_emit_uleb128(function, slot)) {
        bc_set_error(error_buffer, error_buffer_size, message);
        return 0;
    }
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

static int bc_match_function_arg_slot(struct expressionRecord *expression, uint64_t *slot) {
    if(expression == NULL) { return 0; }
    return bc_match_function_arg_slot_core(expression->u.o.base, expression->u.o.location, slot);
}

static int bc_compile_statement_list(struct bc_compile_context *context, struct statementRecord *statement,
                                     struct bc_function *function, int *emitted_terminator, char *error_buffer,
                                     size_t error_buffer_size) {
    for(; statement != NULL; statement = statement->next) {
        if(!bc_compile_statement(context, statement, function, error_buffer, error_buffer_size)) { return 0; }
        if(statement->statementType == returnStatement) { *emitted_terminator = 1; }
    }

    return 1;
}

static int bc_ensure_function_compiled(struct bc_compile_context *context, struct expressionRecord *closure_expression,
                                       size_t *function_index, char *error_buffer, size_t error_buffer_size) {
    char generated_name[64];
    const char *function_name;
    struct bc_function *function;
    int emitted_terminator = 0;
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
    if(!bc_compile_statement_list(context, closure_expression->u.l.code, function, &emitted_terminator, error_buffer,
                                  error_buffer_size)) {
        return 0;
    }

    if(!emitted_terminator && !bc_emit_opcode(function, BC_OP_RETURN)) {
        bc_set_error(error_buffer, error_buffer_size, "unable to emit implicit RETURN opcode");
        return 0;
    }

    return 1;
}

static int bc_compile_assignment_target(struct expressionRecord *target, struct bc_function *function, char *error_buffer,
                                        size_t error_buffer_size) {
    uint64_t slot;

    if(target->operator!= makeReference) { return bc_set_unsupported_expression_error(target, error_buffer, error_buffer_size); }

    if(bc_match_function_local_slot_core(target->u.o.base, target->u.o.location, &slot)
       || ((target->u.o.base != NULL && target->u.o.base->operator== getCurrentContext && target->u.o.location >= 0)
           && (slot = (uint64_t)target->u.o.location, 1))) {
        return bc_emit_slot_store(function, BC_OP_STORE_LOCAL, slot, error_buffer, error_buffer_size,
                                  "unable to emit local store");
    }

    if(bc_match_function_arg_slot_core(target->u.o.base, target->u.o.location, &slot)) {
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
    uint64_t slot;

    if(expression == NULL) { return 1; }

    switch(expression->operator) {
        case getOffset:
        case getGlobalOffset:
            if(bc_match_function_local_slot(expression, &slot)) {
                return bc_emit_slot_load(function, BC_OP_LOAD_LOCAL, slot, error_buffer, error_buffer_size,
                                         "unable to emit local load");
            }
            if(bc_match_function_arg_slot(expression, &slot)) {
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
            return bc_compile_assignment_target(expression->u.a.left, function, error_buffer, error_buffer_size);

        case makeClosure:
            if(expression->u.l.code == NULL) {
                bc_set_error(error_buffer, error_buffer_size, "closure expression has no body");
                return 0;
            }
            return 1;

        case doFunctionCall: {
            size_t function_index;
            uint64_t argument_count = 0;
            struct list *arg;

            if(expression->u.f.fun == NULL || expression->u.f.fun->operator!= makeClosure) {
                bc_set_error(error_buffer, error_buffer_size,
                             "only direct calls to statically known closures are supported in the current bytecode slice");
                return 0;
            }

            for(arg = expression->u.f.args; arg != NULL; arg = arg->next) {
                if(!bc_compile_expression(context, (struct expressionRecord *)arg->value, function, error_buffer,
                                          error_buffer_size)) {
                    return 0;
                }
                argument_count++;
            }

            if(!bc_ensure_function_compiled(context, expression->u.f.fun, &function_index, error_buffer, error_buffer_size)) {
                return 0;
            }

            if(!bc_emit_opcode(function, BC_OP_CALL) || !bc_emit_uleb128(function, (uint64_t)function_index)
               || !bc_emit_uleb128(function, argument_count)) {
                bc_set_error(error_buffer, error_buffer_size, "unable to emit direct function call");
                return 0;
            }
            if(function->max_stack < argument_count + 1u) { function->max_stack = (uint32_t)(argument_count + 1u); }
            return 1;
        }

        default: return bc_set_unsupported_expression_error(expression, error_buffer, error_buffer_size);
    }
}

static int bc_compile_statement(struct bc_compile_context *context, struct statementRecord *statement,
                                struct bc_function *function, char *error_buffer, size_t error_buffer_size) {
    switch(statement->statementType) {
        case makeLocalsStatement:
            if(statement->u.k.size > (int)function->local_count) { function->local_count = (uint32_t)statement->u.k.size; }
            return 1;

        case nullStatement: return 1;

        case expressionStatement:
            if(!bc_compile_expression(context, statement->u.r.e, function, error_buffer, error_buffer_size)) { return 0; }
            if(!bc_emit_opcode(function, BC_OP_POP)) {
                bc_set_error(error_buffer, error_buffer_size, "unable to emit POP for expression statement");
                return 0;
            }
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

int bc_compile_top_level(struct symbolTableRecord *symbols, struct statementRecord *first_statement, struct bc_module *module,
                         char *error_buffer, size_t error_buffer_size) {
    struct bc_compile_context context;
    size_t function_index;
    struct bc_function *function;
    int emitted_terminator = 0;

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

    if(!bc_compile_statement_list(&context, first_statement, function, &emitted_terminator, error_buffer, error_buffer_size)) {
        bc_module_free(module);
        return 0;
    }

    if(!emitted_terminator && !bc_emit_opcode(function, BC_OP_HALT)) {
        bc_set_error(error_buffer, error_buffer_size, "unable to emit halt opcode");
        bc_module_free(module);
        return 0;
    }

    bc_set_error(
        error_buffer, error_buffer_size,
        "compiled bytecode slice: top-level slots, function locals, by-value arguments, direct calls, and local assignments");
    return 1;
}