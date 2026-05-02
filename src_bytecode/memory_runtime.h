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
/// Title: Memory management for the Leda system
// -----------------------------------------------------------------------------

#ifndef LEDA_BYTECODE_MEMORY_RUNTIME_H
#define LEDA_BYTECODE_MEMORY_RUNTIME_H

struct ledaValue {
    int size;
    union {
        int ival;
        double fval;
        char *sval;
        struct ledaValue *oval;
    } data[0];
};

extern struct ledaValue *memoryPointer;
extern struct ledaValue *memoryBase;

#define ROOTSTACKLIMIT 250
extern struct ledaValue *rootStack[];
extern int rootTop;
extern struct ledaValue *globalContext;
extern struct ledaValue *currentContext;

void gcinit(int, int);
struct ledaValue *gcollect(int);
struct ledaValue *staticAllocate(int);

#define gcalloc(sz) \
    (((memoryPointer -= ((sz) + 2)) < memoryBase) ? gcollect(sz) : (memoryPointer->size = (sz) << 2, memoryPointer))

#ifndef gcalloc
extern struct ledaValue *gcalloc(int);
#endif

int yyerror(char *s);

#endif