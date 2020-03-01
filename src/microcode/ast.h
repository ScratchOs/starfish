#ifndef AST_H
#define AST_H

#include <stdbool.h>
#include "microcode/token.h"
#include "shared/memory.h"

struct Parser;

typedef struct BitParameter {
    Token name;
} BitParameter;

typedef struct Bit {
    Token data;
    ARRAY_DEFINE(BitParameter, param);
    SourceRange range;
} Bit;

typedef struct BitArray {
    ARRAY_DEFINE(Bit, data);
    SourceRange range;
} BitArray;

typedef struct Line {
    bool hasCondition;
    BitArray bitsLow;
    BitArray bitsHigh;
    Token conditionErrorToken;
    SourceRange range;
} Line;

typedef struct ASTHeader {
    ARRAY_DEFINE(BitArray, line);
    Token errorPoint;
    SourceRange range;
} ASTHeader;

typedef struct ASTParameter {
    Token name;
    Token value;
    SourceRange range;
} ASTParameter;

typedef struct ASTOpcode {
    Token id;
    Token name;
    ARRAY_DEFINE(Line*, line);
    ARRAY_DEFINE(ASTParameter, param);
    SourceRange range;
} ASTOpcode;

typedef struct ASTTypeEnum {
    Token width;
    ARRAY_DEFINE(Token, member);
    SourceRange range;
} ASTTypeEnum;

typedef enum UserType {
    USER_TYPE_ANY,
    USER_TYPE_ENUM
} UserType;

typedef struct ASTType {
    Token name;

    UserType type;

    union {
        ASTTypeEnum enumType;
    } as;
    SourceRange range;
} ASTType;

typedef struct ASTBitGroupIdentifier {
    enum {
        AST_BIT_GROUP_IDENTIFIER_SUBST,
        AST_BIT_GROUP_IDENTIFIER_LITERAL
    } type;
    Token identifier;
    SourceRange range;
} ASTBitGroupIdentifier;

typedef struct ASTBitGroup {
    Token name;
    ARRAY_DEFINE(ASTParameter, param);
    ARRAY_DEFINE(ASTBitGroupIdentifier, segment);
    SourceRange range;
} ASTBitGroup;

typedef enum ASTStatementType {
    AST_BLOCK_HEADER,
    AST_BLOCK_OPCODE,
    AST_BLOCK_PARAMETER,
    AST_BLOCK_TYPE,
    AST_BLOCK_BITGROUP
} ASTStatementType;

typedef struct ASTStatement {
    ASTStatementType type;

    union {
        ASTHeader header;
        ASTOpcode opcode;
        ASTParameter parameter;
        ASTType type;
        ASTBitGroup bitGroup;
    } as;

    bool isValid;
} ASTStatement;

typedef struct AST {
    ARRAY_DEFINE(const char*, fileName);

    ARRAY_DEFINE(ASTStatement, statement);
} AST;

void InitAST(AST* mcode);

ASTStatement* newStatement(struct Parser* parser, ASTStatementType type);

#endif
