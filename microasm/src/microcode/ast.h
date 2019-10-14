#ifndef AST_H
#define AST_H

#include <stdbool.h>
#include "shared/memory.h"

// TODO: re-write to use list of union based AST

struct Token;

typedef struct BitArray {
    DEFINE_ARRAY(struct Token, data);
} BitArray;

typedef struct Line {
    bool hasCondition;
    BitArray bitsLow;
    BitArray bitsHigh;
    struct Token conditionErrorToken;
} Line;

typedef struct Header {
    DEFINE_ARRAY(BitArray, line);
    bool isValid;
    bool isPresent;
    struct Token errorPoint;
} Header;

typedef struct InputValue {
    struct Token name;
    struct Token value;
} InputValue;

typedef struct Input {
    DEFINE_ARRAY(InputValue, value);
    struct Token inputHeadToken;
    bool isValid;
    bool isPresent;
} Input;

typedef struct OpCode {
    struct Token id;
    struct Token name;
    bool isValid;
    DEFINE_ARRAY(Line*, line);
} OpCode;

typedef struct AST {
    DEFINE_ARRAY(const char*, fileName);

    Header head;
    Input inp;

    DEFINE_ARRAY(OpCode, opcode);
} AST;

void InitAST(AST* mcode);

#endif
