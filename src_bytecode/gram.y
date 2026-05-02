/// Copyright 1993-2015 Timothy A. Budd
// -----------------------------------------------------------------------------
//  This file is part of
/// ---     Leda: Multiparadigm Programming Language
// -----------------------------------------------------------------------------
//
//  Leda is free software: you can redistribute it and/or modify it under the
//  terms of the MIT license, see file "COPYING" included in this distribution.
//
// -----------------------------------------------------------------------------
/// Title: Revised Leda Grammar
///  Description:
//    Revision August 26, 1993
// -----------------------------------------------------------------------------

%token INCLUDEkw DEFINEDkw
%token CONSTkw VARkw TYPEkw CLASSkw FUNCTIONkw OFkw
%token BYNAME BYREF
%token BEGINkw ENDkw RETURNkw IFkw THENkw ELSEkw WHILEkw DOkw ISkw
%token FORkw TOkw
%token BINARYOP PLUSop MINUSop TIMESop
%token ANDop ORop NOT
%token RELATIONALOP LEFTARROW
%token ID CONSTANT CFUNCTIONkw
%token RCONSTANT ICONSTANT SCONSTANT
%token COLON SEMI ASSIGN  COMMA ARROW PERIOD
%token LEFTPAREN RIGHTPAREN LEFTBRACK RIGHTBRACK

%{

#define VERSION "beta leda version 0.71 (23 Sep 2015)\n"

#include "lc_frontend.h"
#include "interp_frontend.h"
#include "memory_runtime.h"

struct symbolTableRecord* syms = 0;
struct symbolTableRecord* globalSyms = 0;
struct symbolTableRecord* bytecode_frontend_symbols = 0;
struct statementRecord* bytecode_frontend_program = 0;

int justParse = 0;

void doInclude(char*);

int yylex (void);

%}

%union
{
    char*                       c;
    struct expressionRecord*    e;
    enum forms                  f;
    int                         i;
    struct list*                l;
    double                      r;
    struct statementRecord*     s;
    struct typeRecord*          t;
    struct symbolRecord*        y;
    struct
    {
        struct statementRecord* first;
        struct statementRecord* last;
    } sp;
}

%type <c> ID SCONSTANT RELATIONALOP BINARYOP
%type <c> ANDop ORop PLUSop MINUSop TIMESop
%type <e> expression relationalExpression basicExpression
%type <e> andExpression notExpression
%type <e> binaryExpression functionCall procedureCall
%type <e> reference plusExpression timesExpression
%type <f> storageForm
%type <i> ICONSTANT IFkw RETURNkw WHILEkw FORkw
%type <l> idlist typelist opttypelist
%type <l> argumentList optexpressionList expressionList
%type <l> typeArguments valueArguments
%type <r> RCONSTANT
%type <s> body
%type <t> type optReturnType returnType
%type <y> classStart classQualifications classheading
%type <sp> statement statements nonReturnStatement nonReturnStatements

%start program

%%
program:
    declarations BEGINkw statements ENDkw SEMI
        {
            bytecode_frontend_symbols = syms;
            bytecode_frontend_program = genBody(syms, $3.first);
        }
    ;

declarations:
    // Nothing
    | declarations declaration
    ;

declaration:
    constdeclarations
    | vardeclarations
    | typedeclarations
    | functiondeclaration
    | classdeclaration
    | INCLUDEkw SCONSTANT SEMI
        { doInclude($2); }
    ;

constdeclarations:
    CONSTkw constdefinitions
    ;

constdefinitions:
    constdefinition
    | constdefinitions constdefinition
    ;

constdefinition:
    ID ASSIGN expression SEMI
        { addConstant(syms, $1, $3); }
    ;

vardeclarations:
    VARkw vardefinitions
    ;

vardefinitions:
    vardefinition
    | vardefinitions vardefinition
    ;

vardefinition:
    idlist COLON type SEMI
        {
            struct list* p;
            for (p = $1; p; p = p->next)
            {
                addVariable(syms, p->value, $3);
            }
        }
    ;

idlist:
    ID
        { $$ = newList($1, 0); }
    | idlist COMMA ID
        { $$ = newList($3, $1); }
    ;

typedeclarations:
    TYPEkw typedefinitions
    ;

typedefinitions:
    typedefinition
    | typedefinitions typedefinition
    ;

typedefinition:
    ID COLON type SEMI
        { addTypeDeclaration(syms, $1, $3); }
    ;

type:
    ID
        { $$ = checkType(lookupSymbol(syms, $1)); }
    | ID LEFTBRACK typelist RIGHTBRACK
        { $$ = checkQualifications(checkType(lookupSymbol(syms, $1)), $3); }
    | FUNCTIONkw opttypelist
        { $$ = newFunctionType($2, 0); }
    | FUNCTIONkw opttypelist ARROW type
        { $$ = newFunctionType($2, $4); }
    ;

opttypelist:
    LEFTPAREN RIGHTPAREN
        { $$ = 0; }
    | LEFTPAREN typelist RIGHTPAREN
        { $$ = $2; }
    ;

typelist:
    storageForm type
        { $$ = newTypelist($2, $1, 0); }
    | typelist COMMA storageForm type
        { $$ = newTypelist($4, $3, $1); }
    ;

functiondeclaration:
    functionHead declarations body SEMI
        {
            (syms->u.f.theFunctionSymbol)->u.f.code->next = genBody(syms, $3);
            syms = syms->surroundingContext;
        }
    ;

functionHead:
    functionname valueArguments optReturnType SEMI
        { addFunctionArguments(syms, $2, $3); }
    ;

functionname:
    FUNCTIONkw ID typeArguments
        { syms = addFunctionSymbol(syms, $2, $3); }
    ;

typeArguments:
    // nothing
        { $$ = 0; }
    | LEFTBRACK argumentList RIGHTBRACK
        { $$ = $2; }
    ;

argumentList:
    storageForm idlist COLON type
        { $$ = buildArgumentList($2, $1, $4, 0); }
    | argumentList COMMA storageForm idlist COLON type
        { $$ = buildArgumentList($4, $3, $6, $1); }
    ;

storageForm:
    // Nothing
        {$$ = byValue; }
    | BYNAME
        {$$ = byName; }
    | BYREF
        {$$ = byReference; }
    ;

valueArguments:
    LEFTPAREN RIGHTPAREN
        { $$ = 0; }
    | LEFTPAREN argumentList RIGHTPAREN
        { $$ = $2; }
    ;

optReturnType:
    // Nothing
        { $$ = 0; }
    | returnType
        { $$ = $1; }
    ;

returnType:
    ARROW type
        { $$ = $2; }
    ;

classdeclaration:
    classheading declarations ENDkw SEMI
        {
            buildClassTable($1);
            syms = syms->surroundingContext;
        }
    ;

classheading:
    classQualifications SEMI
        {
            struct typeRecord* t = checkType(lookupSymbol(syms, "object"));
            if (checkClass(t) == 0)
            {
                yyerror("unable to find class `object'");
            }
            $$ = $1;
            fillInParent(syms->definingType, t, 0);
        }
    | classQualifications OFkw ID SEMI
        {
            struct typeRecord* t = checkType(lookupSymbol(syms, $3));
            if (checkClass(t) == 0)
            {
                yyserror
                (
                    "non class identifier %s used where class expected",
                    $3
                );
            }
            $$ = $1;
            fillInParent(syms->definingType, t, 0);
        }
    | classQualifications OFkw ID LEFTBRACK typelist RIGHTBRACK SEMI
        {
            struct typeRecord* t = checkType(lookupSymbol(syms, $3));
            if (checkClass(t) == 0)
            {
                yyserror
                (
                    "non class identifier %s used where class expected",
                    $3
                );
            }
            $$ = $1;
            fillInParent(syms->definingType, t, $5);
        }
    ;

classQualifications:
    classStart typeArguments
        {
            $$ = $1;
            if ($2 != 0)
            {
                $$->u.c.typ = newQualifiedType(syms, $2, $1->u.c.typ);
            }
        }
    ;

classStart:
    CLASSkw ID
        {
            $$ = newClassSymbol(syms, globalSyms, $2);
            syms = $$->u.c.typ->u.c.symbols;
        }
    ;

body:
    BEGINkw statements ENDkw
        { $$ = $2.first; }
    | BEGINkw ENDkw
        { $$ = newStatement(nullStatement); }
    ;

statements:
    statement SEMI
        {$$ = $1;}
    | statements statement SEMI
        {
            $$.first = $1.first; $$.last = $2.last;
            $1.last->next = $2.first;
        }
    ;

nonReturnStatements:
    nonReturnStatement SEMI
        {$$ = $1;}
    | nonReturnStatements nonReturnStatement SEMI
        {
            $$.first = $1.first; $$.last = $2.last;
            $1.last->next = $2.first;
        }
    ;

statement:
    reference ASSIGN expression
        { $$.first = $$.last = genAssignmentStatement($1, $3); }
    | RETURNkw
        { $$.first = $$.last = genReturnStatement(syms, 0); }
    | RETURNkw expression
        { $$.first = $$.last = genReturnStatement(syms, $2); }
    | BEGINkw statements ENDkw
        {
            $$.first = $2.first;
            $$.last = $2.last;
        }
    | BEGINkw ENDkw
        { $$.first = $$.last = newStatement(nullStatement);}
    | IFkw expression THENkw statement
        {
            $$.last = newStatement(nullStatement);
            $$.first = genConditionalStatement
            (
                $1,
                booleanCheck(syms, $2),
                $4.first, $4.last,
                0, 0,
                $$.last
            );
        }
    | IFkw expression THENkw statement ELSEkw statement
        {
            $$.last = newStatement(nullStatement);
            $$.first = genConditionalStatement
            (
                $1,
                booleanCheck(syms, $2),
                $4.first,
                $4.last,
                $6.first,
                $6.last,
                $$.last
            );
        }
    | WHILEkw expression DOkw statement
        {
            $$.last = newStatement(nullStatement);
            $$.first = genWhileStatement
            (
                $1,
                booleanCheck(syms, $2),
                $4.first,
                $4.last,
                $$.last
            );
        }
    | FORkw expression DOkw nonReturnStatement
        {
            $$.first = $$.last = genExpressionStatement
            (
                generateForRelation(syms, $2, 0, $4.first, $4.last)
            );
        }
    | FORkw expression TOkw expression DOkw nonReturnStatement
        {
            $$.first = $$.last = genExpressionStatement
            (
                generateForRelation(syms, $2, $4, $6.first, $6.last)
            );
        }
    | FORkw reference ASSIGN expression TOkw expression DOkw statement
        {
            $$.last = newStatement(nullStatement);
            $$.first = generateArithmeticForStatement
            (
                $1,
                syms,
                $2,
                $4,
                $6,
                $8.first,
                $8.last,
                $$.last
            );
        }
    | procedureCall
        { $$.first = $$.last = genExpressionStatement($1);}
    | // Empty statement
        { $$.first = $$.last = newStatement(nullStatement); }
    ;

nonReturnStatement:
    reference ASSIGN expression
        { $$.first = $$.last = genAssignmentStatement($1, $3); }
    | BEGINkw nonReturnStatements ENDkw
        {
            $$.first = $2.first;
            $$.last = $2.last;
        }
    | BEGINkw ENDkw
        { $$.first = $$.last = newStatement(nullStatement);}
    | IFkw expression THENkw nonReturnStatement
        {
            $$.last = newStatement(nullStatement);
            $$.first = genConditionalStatement
            (
                $1,
                booleanCheck(syms, $2),
                $4.first, $4.last, 0, 0, $$.last
            );
        }
    | IFkw expression THENkw nonReturnStatement ELSEkw nonReturnStatement
        {
            $$.last = newStatement(nullStatement);
            $$.first = genConditionalStatement
            (
                $1,
                booleanCheck(syms, $2),
                $4.first, $4.last,
                $6.first, $6.last, $$.last
            );
        }
    | WHILEkw expression DOkw nonReturnStatement
        {
            $$.last = newStatement(nullStatement);
            $$.first = genWhileStatement
            (
                $1,
                booleanCheck(syms, $2), $4.first, $4.last,
                    $$.last
            );
        }
    | FORkw expression DOkw nonReturnStatement
        {
            $$.first = $$.last = genExpressionStatement
            (
                generateForRelation(syms, $2, 0, $4.first, $4.last)
            );
        }
    | FORkw expression TOkw expression DOkw nonReturnStatement
        {
            $$.first = $$.last = genExpressionStatement
            (
                generateForRelation(syms, $2, $4, $6.first, $6.last)
            );
        }
    | FORkw reference ASSIGN expression TOkw expression DOkw nonReturnStatement
        {
            $$.last = newStatement(nullStatement);
            $$.first = generateArithmeticForStatement
            (
                $1,
                syms,
                $2, $4, $6, $8.first, $8.last, $$.last
            );
        }
    | procedureCall
        { $$.first = $$.last = genExpressionStatement($1);}
    | // Empty statement
        { $$.first = $$.last = newStatement(nullStatement); }
    ;

optexpressionList:
    // Nothing
        { $$ = 0; }
    | expressionList
        { $$ = $1; }
    ;

expressionList:
    expression
        { $$ = newList((char*) $1, 0); }
    | expressionList COMMA expression
        { $$ = newList((char*) $3, $1); }
    ;

expression:
    andExpression
        { $$ = $1; }
    | expression ORop andExpression
        {$$ = generateBinaryOperator(syms, $2, $1, $3);}
    ;

andExpression:
    notExpression
        { $$ = $1; }
    | andExpression ANDop notExpression
        {$$ = generateBinaryOperator(syms, $2, $1, $3);}
    ;

notExpression:
    relationalExpression
        { $$ = $1; }
    | NOT notExpression
        { $$ = generateUnaryOperator(syms, "not", $2); }
    | reference ISkw ID LEFTPAREN idlist RIGHTPAREN
        {
            $$ = genPatternMatch(syms, $1,
            lookupIdentifier(syms, $3), $5);
        }
    | reference ISkw ID
        {
            $$ = genPatternMatch(syms, $1,
            lookupIdentifier(syms, $3), 0);
        }
    ;

relationalExpression:
    binaryExpression
        {$$ = $1;}
    | binaryExpression RELATIONALOP binaryExpression
        {$$ = generateBinaryOperator(syms, $2, $1, $3);}
    | reference LEFTARROW binaryExpression
        {$$ = generateLeftArrow(syms, $1, $3); }
    ;

binaryExpression:
    plusExpression
        {$$ = $1;}
    | binaryExpression BINARYOP plusExpression
        {$$ = generateBinaryOperator(syms, $2, $1, $3);}
    ;

plusExpression:
    timesExpression
        {$$ = $1;}
    | plusExpression PLUSop timesExpression
        {$$ = generateBinaryOperator(syms, $2, $1, $3);}
    | plusExpression MINUSop timesExpression
        {$$ = generateBinaryOperator(syms, $2, $1, $3);}
    ;

timesExpression:
    functionCall
        {$$ = $1;}
    | timesExpression TIMESop functionCall
        {$$ = generateBinaryOperator(syms, $2, $1, $3);}
    | MINUSop functionCall
        { $$ = generateUnaryOperator(syms, "negation", $2); }
    ;

procedureCall:
    functionCall LEFTPAREN optexpressionList RIGHTPAREN
        {$$ = generateFunctionCall(syms, $1, $3, 0); }
    | CFUNCTIONkw ID LEFTPAREN optexpressionList RIGHTPAREN
        {$$ = generateCFunctionCall($2, $4, 0);}
    ;

functionCall:
    basicExpression
        {$$ = $1; }
    | DEFINEDkw LEFTPAREN expression RIGHTPAREN
        {
            $$ = newExpression(doSpecialCall);
            $$->u.c.index = 22;
            $$->u.c.args = newList((char*) $3, 0);
            $$->resultType = booleanType;
        }
    | functionCall LEFTPAREN optexpressionList RIGHTPAREN
        {$$ = generateFunctionCall(syms, $1, $3, 1); }
    | CFUNCTIONkw ID LEFTPAREN optexpressionList RIGHTPAREN returnType
        {$$ = generateCFunctionCall($2, $4, $6);}
    ;

basicExpression:
    reference
        {$$ = $1; }
    | ICONSTANT
        {$$ = integerConstant($1);}
    | SCONSTANT
        {$$ = stringConstant($1);}
    | RCONSTANT
        {$$ = realConstant($1); }
    | LEFTPAREN expression RIGHTPAREN
        {$$ = $2;}
    | functionExpressionHead declarations body
        {
            $$ = newExpression(makeClosure);
            $$->u.l.context = newExpression(getCurrentContext);
            $$->u.l.code = genBody(syms, $3);
            $$->resultType = syms->definingType;
            syms = syms->surroundingContext;
        }
    | basicExpression LEFTBRACK typelist RIGHTBRACK
        {
            $$ = $1;
            $$->resultType = checkQualifications($$->resultType, $3);
        }
    | LEFTBRACK expressionList RIGHTBRACK
        {$$ = generateArrayLiteral(syms, $2);}
    ;

reference:
    ID
        {$$ = lookupIdentifier(syms, $1); }
    | ID COLON type
        {
            addVariable(syms, $1, $3);
            $$ = lookupIdentifier(syms, $1);
        }
    | functionCall PERIOD ID
        {
            $$ = lookupField($1, $1->resultType, $3);
            if ($$ == 0)
            {
                yyserror("unknown field name used: %s", $3);
            }
        }
    ;

functionExpressionHead:
    FUNCTIONkw valueArguments optReturnType SEMI
        {syms = generateFunctionExpression(syms, $2, $3);}
    ;

%%

#include "lc_frontend.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void yyserror(char* pattern, char* name)
{
    fprintf(stderr,"%s:%d:[%s] ", fileName, linenumber, yytext);
    fprintf(stderr, pattern, name);
    fprintf(stderr, "\n");
    exit(1);
}

int yyerror(char* s)
{
    fprintf(stderr,"%s:%d:[%s] %s\n", fileName, linenumber, yytext, s);
    exit(1);
}

static char* includeDirectories[10];
static int includeDirTop = 0;
static char inputDirectory[1024];

static void setInputDirectory(char* path)
{
    char* slash;

    includeDirTop = 0;
    inputDirectory[0] = '\0';
    if (path == 0)
    {
        return;
    }

    strncpy(inputDirectory, path, sizeof(inputDirectory) - 1);
    inputDirectory[sizeof(inputDirectory) - 1] = '\0';
    slash = strrchr(inputDirectory, '/');
    if (slash != 0)
    {
        *slash = '\0';
        includeDirectories[includeDirTop++] = inputDirectory;
    }
}

static int testInclude(char* name)
{
    FILE * fid;
    fid = fopen(name, "r");

    if (fid != NULL)
    {
        fclose(fid);
        openInputFile(name);
        return 1;
    }

    fclose(fid);

    return 0;
}

void doInclude(char* name)
{
    int i;
    char namebuffer[256];

    if (testInclude(name)) return;
    for (i = 0; i < includeDirTop; i++)
    {
        strcpy(namebuffer, includeDirectories[i]);
        strcat(namebuffer, "/");
        strcat(namebuffer, name);
        if (testInclude(namebuffer)) return;
    }

    yyserror("unable to open include file %s", name);
}

int bytecode_parse_file(char* input_path, struct symbolTableRecord** out_symbols, struct statementRecord** out_statement)
{
    justParse = 1;
    bytecode_frontend_symbols = 0;
    bytecode_frontend_program = 0;
    setInputDirectory(input_path);
    openInputFile(input_path);
    globalSyms = syms = initialCreation();
    yyparse();

    if(out_symbols != 0) { *out_symbols = bytecode_frontend_symbols; }
    if(out_statement != 0) { *out_statement = bytecode_frontend_program; }

    return bytecode_frontend_program != 0;
}


// -----------------------------------------------------------------------------
