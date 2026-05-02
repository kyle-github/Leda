#include "interp_frontend.h"

char *specialFunctionNames[] = {"Leda_object_equals",    "Leda_string_compare",   "Leda_string_print",   "Leda_string_concat",
                                "Leda_integer_equals",   "Leda_integer_plus",     "Leda_integer_minus",  "Leda_integer_times",
                                "Leda_integer_divide",   "Leda_integer_asString", "Leda_integer_less",   "Leda_integer_or",
                                "Leda_integer_and",      "Leda_integer_not",      "Leda_integer_asReal", "Leda_object_allocate",
                                "Leda_object_at",        "Leda_object_atPut",     "Leda_object_cast",    "Leda_string_length",
                                "Leda_string_substring", "Leda_stdin_read",       "Leda_object_defined", "Leda_real_asString",
                                "Leda_real_plus",        "Leda_real_minus",       "Leda_real_times",     "Leda_real_divide",
                                "Leda_real_less",        "Leda_real_asInteger",   "Leda_real_equals",    0};

void buildClassTable(struct symbolRecord *sym) {
    struct typeRecord *type;

    if(sym->styp != classDefSymbol) { yyerror("build table on non class def"); }

    type = sym->u.c.typ;
    if(type->ttyp == qualifiedType) { type = type->u.q.baseType; }
    if(type->ttyp != classType) { yyerror("build table on non class type"); }

    /*
     * The bytecode frontend only needs class metadata to remain structurally
     * valid during parsing and semantic analysis. Runtime method-table object
     * allocation will move into the bytecode runtime later.
     */
    type->u.c.staticTable = 0;
}