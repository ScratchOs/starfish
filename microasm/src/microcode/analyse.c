#include "microcode/analyse.h"

#include <string.h>
#include "shared/table.h"
#include "shared/memory.h"
#include "shared/graph.h"
#include "shared/log.h"
#include "emulator/compiletime/create.h"
#include "microcode/token.h"
#include "microcode/ast.h"
#include "microcode/parser.h"
#include "microcode/error.h"

// TODO fix opcode ids so they are correct between ast and vmcoregen
// TODO add bitgroup analysis
// TODO finish opcode analysis, parameters, paramatised bits

typedef struct IdentifierParameter {
    unsigned int value;
    Token* definition;
} IdentifierParameter;

typedef struct IdentifierControlBit {
    unsigned int value;
} IdentifierControlBit;

typedef struct IdentifierEnum {
    Token* definition;
} IdentifierEnum;

typedef struct IdentifierBitGroup {
    Token* definition;
} IdentifierBitGroup;

typedef enum IdentifierType {
    TYPE_PARAMETER,
    TYPE_VM_CONTROL_BIT,
    TYPE_ENUM,
    TYPE_BITGROUP
} IdentifierType;

char *IdentifierTypeNames[] = {
    "parameter",
    "vm control bit",
    "enum",
    "bitgroup"
};

typedef struct Identifier {
    IdentifierType type;
    union {
        IdentifierParameter parameter;
        IdentifierControlBit control;
        IdentifierEnum enumType;
        IdentifierBitGroup bitgroup;
    } as;
} Identifier;

Table identifiers;

Table erroredParameters;
static Error errParamMissing = {0};
static Error errParameterWrongType = {0};
static void getParameterErrors() {
    initTable(&erroredParameters, strHash, strCmp);
    newErrAt(&errParamMissing, ERROR_SEMANTIC, "Parameter '%s' required to parse %s not found");
    newErrAt(&errParameterWrongType, ERROR_SEMANTIC, "To parse %s, '%s' is required as a "
        "parameter, but it is defined as a %s");
}

static Identifier* getParameter(Parser* parser, Token* errPoint, char* name, char* usage) {
    if(tableHas(&erroredParameters, name)) {
        return NULL;
    }

    Identifier* value;
    if(!tableGet(&identifiers, name, (void**)&value)) {
        error(parser, &errParamMissing, errPoint, name, usage);
        tableSet(&erroredParameters, name, (void*)1);
        return NULL;
    }

    if(value->type == TYPE_PARAMETER) {
        return value;
    }
    error(parser, &errParameterWrongType, errPoint, usage, name, IdentifierTypeNames[value->type]);
    tableSet(&erroredParameters, name, (void*)1);
    return NULL;
}

static Error errDuplicateDefine = {0};
static void alreadyDefinedErrors() {
    newErrAt(&errDuplicateDefine, ERROR_SEMANTIC, "One or more prior definitions for '%s' found, "
        "currently declared as being of type %s");
}

static void alreadyDefined(Parser* parser, char* name, Identifier* current, ASTStatement* s) {
    Token* errLoc;
    switch(s->type) {
        case AST_BLOCK_BITGROUP: errLoc = &s->as.bitGroup.name; break;
        case AST_BLOCK_HEADER: errLoc = &s->as.header.errorPoint; break;
        case AST_BLOCK_OPCODE: errLoc = &s->as.opcode.name; break;
        case AST_BLOCK_PARAMETER: errLoc = &s->as.parameter.name; break;
        case AST_BLOCK_TYPE: errLoc = &s->as.type.name; break;
    }

    switch(current->type) {
        case TYPE_PARAMETER:
            error(parser, &errDuplicateDefine, errLoc, 
                name, IdentifierTypeNames[current->type]);
            break;
        case TYPE_VM_CONTROL_BIT:
            error(parser, &errDuplicateDefine, errLoc,
                name, IdentifierTypeNames[current->type]);
            break;
        case TYPE_ENUM:
            error(parser, &errDuplicateDefine, errLoc,
                name, IdentifierTypeNames[current->type]);
            break;
        case TYPE_BITGROUP:
            error(parser, &errDuplicateDefine, errLoc,
                name, IdentifierTypeNames[current->type]);
            break;
    }
}

static Error errDuplicateParameter = {0};
static Error errParameterIsControl = {0};
static Error errParameterIsEnum = {0};
static Error errParameterIsBitgroup = {0};
static void analyseParameterErrors() {
    newErrAt(&errDuplicateParameter, ERROR_SEMANTIC, "Duplicate value for parameter found");
    newErrNoteAt(&errDuplicateParameter, "Originally defined here");
    newErrAt(&errParameterIsControl, ERROR_SEMANTIC,
        "Parameter name is defined as the name for a control bit");
    newErrAt(&errParameterIsEnum, ERROR_SEMANTIC, "Parameter name already defined as an enum");
    newErrNoteAt(&errParameterIsEnum, "Originally defined here");
    newErrAt(&errParameterIsBitgroup, ERROR_SEMANTIC, "Parameter name is a bitgroup");
    newErrNoteAt(&errParameterIsBitgroup, "Originally defined here");
}

static void analyseParameter(Parser* parser, ASTStatement* s) {
    CONTEXT(INFO, "Analysing parameter");

    char* key = (char*)s->as.parameter.name.data.string;

    Identifier* current;
    if(tableGet(&identifiers, key, (void**)&current)) {
        alreadyDefined(parser, key, current, s);
        return;
    }

    Identifier* value = ArenaAlloc(sizeof(Identifier));
    value->type = TYPE_PARAMETER;
    value->as.parameter.definition = &s->as.parameter.name;
    value->as.parameter.value = s->as.parameter.value.data.value;
    tableSet(&identifiers, key, (void*)value);
}

static Error errNoOrdering = {0};
static Error errBusRead = {0};
static void analyseLineErrors() {
    newErrAt(&errNoOrdering, ERROR_SEMANTIC, "Unable to order microcode bits in line %u");
    newErrAt(&errBusRead, ERROR_SEMANTIC, "Command reads from bus before it was written in line %u");
}

static NodeArray analyseLine(VMCoreGen* core, Parser* mcode, BitArray* line, Token* opcodeName, unsigned int lineNumber) {
    CONTEXT(INFO, "Analysing line");

    Graph graph;
    InitGraph(&graph);

    for(unsigned int i = 0; i < line->dataCount; i++) {
        Identifier* value;
        tableGet(&identifiers, (char*)line->datas[i].data.data.string, (void**)&value);
        unsigned int command = value->as.control.value;
        AddNode(&graph, command);
        for(unsigned int j = 0; j < core->commands[command].changesLength; j++) {
            unsigned int changed = core->commands[command].changes[j];
            for(unsigned int k = 0; k < line->dataCount; k++) {
                Identifier* value;
                tableGet(&identifiers, (char*)line->datas[k].data.data.string, (void**)&value);
                unsigned int comm = value->as.control.value;
                for(unsigned int l = 0; l < core->commands[comm].dependsLength; l++) {
                    unsigned int depended = core->commands[comm].depends[l];
                    if(changed == depended) {
                        AddEdge(&graph, command, comm);
                    }
                }
            }
        }
    }

    NodeArray nodes = TopologicalSort(&graph);
    if(!nodes.validArray) {
        error(mcode, &errNoOrdering, opcodeName, lineNumber);
    }

    for(unsigned int i = 0; i < core->componentCount; i++) {
        Component* component = &core->components[i];
        component->busStatus = false;
    }

    for(unsigned int i = 0; i < nodes.nodeCount; i++) {
        Command* command = &core->commands[nodes.nodes[i]->value];

        for(unsigned int j = 0; j < command->readsLength; j++) {
            Component* bus = &core->components[command->reads[j]];
            if(!bus->busStatus) {
                error(mcode, &errBusRead, opcodeName, lineNumber);
            }
        }

        for(unsigned int j = 0; j < command->writesLength; j++) {
            Component* bus = &core->components[command->writes[j]];
            bus->busStatus = true;
        }
    }

    return nodes;
}

static Error errIdentifierNotDefined = {0};
static Error errMCodeBitIsParameter = {0};
static Error errMCodeBitIsEnum = {0};
static Error errMCodeBitIsBitgroup = {0};
static void mcodeBitArrayCheckErrors() {
    newErrAt(&errIdentifierNotDefined, ERROR_SEMANTIC, "Identifier was not defined");
    newErrAt(&errMCodeBitIsParameter, ERROR_SEMANTIC, 
        "Identifier previously defined as a parameter, control bit required");
    newErrNoteAt(&errMCodeBitIsParameter, "Defined here");
    newErrAt(&errMCodeBitIsEnum, ERROR_SEMANTIC, 
        "Identifier previously defined as an enum, control bit required");
    newErrNoteAt(&errMCodeBitIsEnum, "Defined here");
    newErrAt(&errMCodeBitIsBitgroup, ERROR_SEMANTIC, 
        "Identifier previously defined as a bit group, control bit required");
    newErrNoteAt(&errMCodeBitIsBitgroup, "Defined here");
}

static bool mcodeBitArrayCheck(Parser* parser, BitArray* arr) {
    CONTEXT(INFO, "Checking bit array");

    bool passed = true;

    for(unsigned int j = 0; j < arr->dataCount; j++) {
        Token* bit = &arr->datas[j].data;

        Identifier* val;
        if(!tableGet(&identifiers, (char*)bit->data.string, (void**)&val)) {
            error(parser, &errIdentifierNotDefined, bit);
            passed = false;
            continue;
        }
        switch(val->type) {
            case TYPE_VM_CONTROL_BIT: break;
            case TYPE_PARAMETER:
                error(parser, &errMCodeBitIsParameter, 
                    bit, val->as.parameter.definition);
                passed = false;
                break;
            case TYPE_ENUM:
                error(parser, &errMCodeBitIsEnum,
                    bit, val->as.enumType.definition);
                passed = false;
                break;
            case TYPE_BITGROUP:
                error(parser, &errMCodeBitIsBitgroup,
                    bit, val->as.bitgroup.definition);
                passed = false;
                break;
        }
    }

    return passed;
}

static Error errDuplicateHeader = {0};
static Error errHeaderLineCount = {0};
static Error errHeaderWrongType = {0};
static void analyseHeaderErrors() {
    newErrAt(&errDuplicateHeader, ERROR_SEMANTIC, 
        "Cannot have more than one header statement in a microcode");
    newErrNoteAt(&errDuplicateHeader, "Header first included here");
    newErrAt(&errHeaderLineCount, ERROR_SEMANTIC, 
        "Number of lines in header (%u) is too high, the maximum is %u");
    newErrAt(&errHeaderWrongType, ERROR_SEMANTIC, "Cannot use non output bit in header statement");
    newErrNoteAt(&errHeaderWrongType, "Previously declared here");
}

static bool parsedHeader = false;
static ASTStatement* firstHeader = NULL;
static void analyseHeader(Parser* parser, ASTStatement* s, VMCoreGen* core) {
    CONTEXT(INFO, "Analysing header");

    if(parsedHeader) {
        error(parser, &errDuplicateHeader, &s->as.header.errorPoint,
            firstHeader->as.header.errorPoint);
    }
    parsedHeader = true;
    firstHeader = s;

    Identifier* phase = getParameter(parser, &s->as.header.errorPoint, "phase", "header");
    if(phase == NULL) return;
    unsigned int maxLines = (1 << phase->as.parameter.value);

    if(s->as.header.lineCount > maxLines) {
        error(parser, &errHeaderLineCount, &s->as.header.errorPoint,
            s->as.header.lineCount, maxLines);
    }

    for(unsigned int i = 0; i < s->as.header.lineCount; i++) {
        BitArray* line = &s->as.header.lines[i];

        if(!mcodeBitArrayCheck(parser, line)) {
            continue;
        }

        NodeArray nodes = analyseLine(core, parser, line, &s->as.header.errorPoint, i);
        for(unsigned int j = 0; j < nodes.nodeCount; j++) {
            PUSH_ARRAY(unsigned int, *core, headBit, nodes.nodes[j]->value);
        }
    }
}

static Error errOpcodeIdSize = {0};
static Error errOpcodeLineCount = {0};
static Error errOpcodeWrongType = {0};
static void analyseOpcodeErrors() {
    newErrAt(&errOpcodeIdSize, ERROR_SEMANTIC, "Opcode id is too large");
    newErrAt(&errOpcodeLineCount, ERROR_SEMANTIC, "Number of lines in opcode is too high");
    newErrAt(&errOpcodeWrongType, ERROR_SEMANTIC, "Opcode bits must be outputs");
    newErrNoteAt(&errOpcodeWrongType, "Previously declared here");
}

static void analyseOpcode(Parser* parser, ASTStatement* s, VMCoreGen* core) {
    CONTEXT(INFO, "Analysing opcode statement");

    if(!parsedHeader) return;
    Identifier* phase = getParameter(parser, &s->as.opcode.name, "phase", "opcode");
    if(phase == NULL) return;
    unsigned int maxLines = (1 << phase->as.parameter.value) - 
        firstHeader->as.parameter.value.data.value;

    Identifier* opsize = getParameter(parser, &s->as.opcode.name, "opsize", "opcode");
    if(opsize == NULL) return;
    unsigned int maxOpCodes = (1 << opsize->as.parameter.value);

    GenOpCode gencode;
    ARRAY_ALLOC(GenOpCodeLine*, gencode, line);

    if(s->as.opcode.id.data.value >= maxOpCodes) {
        error(parser, &errOpcodeIdSize, &s->as.opcode.id);
    }

    if(s->as.opcode.lineCount > maxLines) {
        error(parser, &errOpcodeLineCount, &s->as.opcode.name);
    }

    gencode.id = s->as.opcode.id.data.value;
    gencode.name = s->as.opcode.name.start;
    gencode.nameLen = s->as.opcode.name.length;

    for(unsigned int j = 0; j < s->as.opcode.lineCount; j++) {
        Line* line = s->as.opcode.lines[j];
        GenOpCodeLine* genline = ArenaAlloc(sizeof(GenOpCodeLine));
        genline->hasCondition = line->hasCondition;

        if(!mcodeBitArrayCheck(parser, &line->bitsLow)) {
            continue;
        }
        NodeArray lowNodes = analyseLine(core, parser, &line->bitsLow, &s->as.opcode.name, j);
        ARRAY_ALLOC(unsigned int, *genline, lowBit);
        for(unsigned int k = 0; k < lowNodes.nodeCount; k++) {
            PUSH_ARRAY(unsigned int, *genline, lowBit, lowNodes.nodes[k]->value);
        }

        if(line->hasCondition) {
            if(!mcodeBitArrayCheck(parser, &line->bitsHigh)) {
                continue;
            }
            NodeArray highNodes = analyseLine(core, parser, &line->bitsHigh, &s->as.opcode.name, j);
            ARRAY_ALLOC(unsigned int, *genline, highBit);
            for(unsigned int k = 0; k < highNodes.nodeCount; k++) {
                PUSH_ARRAY(unsigned int, *genline, highBit, highNodes.nodes[k]->value);
            }
        } else {
            genline->highBitCapacity = genline->lowBitCapacity;
            genline->highBitCount = genline->lowBitCount;
            genline->highBits = genline->lowBits;
        }
        PUSH_ARRAY(GenOpCodeLine*, gencode, line, genline);
    }

    PUSH_ARRAY(GenOpCode, *core, opcode, gencode);
}

static Error errEnumMoreMembers = {0};
static Error errEnumLessMembers = {0};
static Error errEnumDuplicate = {0};
static void analyseTypeErrors() {
    newErrAt(&errEnumMoreMembers, ERROR_SEMANTIC, "Enum statement requires %u members, got %u");
    newErrAt(&errEnumLessMembers, ERROR_SEMANTIC, "Enum statement requires %u members, got %u");
    newErrAt(&errEnumDuplicate, ERROR_SEMANTIC, "Duplicated enum member");
    newErrNoteAt(&errEnumDuplicate, "Originaly defined here");
}

static void analyseEnum(Parser* parser, ASTStatement* s) {
    CONTEXT(INFO, "Analysing enum statement");

    ASTType* typeStatement = &s->as.type;
    ASTTypeEnum* enumStatement = &typeStatement->as.enumType;

    Token* type = &s->as.type.name;
    Identifier* value;
    if(tableGet(&identifiers, (char*)type->data.string, (void**)&value)) {
        alreadyDefined(parser, (char*)type->data.string, value, s);
        return;
    }

    value = ArenaAlloc(sizeof(Identifier));
    value->type = TYPE_ENUM;
    value->as.enumType.definition = type;
    tableSet(&identifiers, (char*)type->data.string, (void*)value);

    unsigned int size = enumStatement->width.data.value;
    unsigned int requiredMemberCount = size == 1 ? 2 : 1 << size;
    if(enumStatement->memberCount != requiredMemberCount) {
        if(enumStatement->memberCount < requiredMemberCount) {
            error(parser, &errEnumMoreMembers, &typeStatement->name, 
                requiredMemberCount, enumStatement->memberCount);
        } else {
            error(parser, &errEnumLessMembers, &enumStatement->members[requiredMemberCount], 
                requiredMemberCount, enumStatement->memberCount);
        }
    }

    // as enums cannot be used directly, no issues if
    // enum values collide with other identifiers

    Table members;
    initTable(&members, tokenHash, tokenCmp);

    for(unsigned int i = 0; i < enumStatement->memberCount; i++) {
        Token* tok = &enumStatement->members[i];
        if(tableHas(&members, tok)) {
            Token* original;
            tableGetKey(&members, tok, (void**)&original);
            error(parser, &errEnumDuplicate, tok, original);
        } else {
            tableSet(&members, tok, (void*)1);
        }
    }
}

static void analyseType(Parser* parser, ASTStatement* s) {
    CONTEXT(INFO, "Analysing type statement");

    switch(s->as.type.type) {
        case AST_BLOCK_TYPE_ENUM: analyseEnum(parser, s); break;
    }
}

static void analyseBitgroupErrors() {

}

static void analyseBitgroup(Parser* parser, ASTStatement* s) {
    CONTEXT(INFO, "Analysing type statement");

    (void) parser;
    Identifier* value = ArenaAlloc(sizeof(Identifier));
    value->type = TYPE_BITGROUP;
    value->as.bitgroup.definition = &s->as.type.name;
    tableSet(&identifiers, (char*)s->as.type.name.data.string, (void*)value);
}

static Error errNoHeader = {0};
static Error errNoOpsize = {0};
static Error errNoPhase = {0};
static void analyseCheckParsedErrors() {
    newErrEnd(&errNoHeader, ERROR_SEMANTIC, "A header statement is required, but not found");
    newErrEnd(&errNoOpsize, ERROR_SEMANTIC, "An opsize parameter is required, but not found");
    newErrEnd(&errNoPhase, ERROR_SEMANTIC, "A phase parameter is required, but not found");
}

static bool errorsInitialised;
typedef void (*errorInitialiser)();
static errorInitialiser errorInitialisers[] = {
    getParameterErrors,
    alreadyDefinedErrors,
    analyseParameterErrors,
    analyseLineErrors,
    mcodeBitArrayCheckErrors,
    analyseHeaderErrors,
    analyseOpcodeErrors,
    analyseTypeErrors,
    analyseBitgroupErrors,
    analyseCheckParsedErrors
};

void InitAnalysis() {
    if(!errorsInitialised) {
        for(unsigned int i = 0; i < sizeof(errorInitialisers)/sizeof(errorInitialiser); i++) {
            errorInitialisers[i]();
        }
    }
}

void Analyse(Parser* parser, VMCoreGen* core) {
    CONTEXT(INFO, "Running analysis");

    if(parser->hadError)return;
    if(!errorsInitialised)InitAnalysis();

    initTable(&identifiers, strHash, strCmp);

    for(unsigned int i = 0; i < core->commandCount; i++) {
        char* key = (char*)core->commands[i].name;
        Identifier* value = ArenaAlloc(sizeof(Identifier));
        value->type = TYPE_VM_CONTROL_BIT;
        value->as.control.value = i;
        tableSet(&identifiers, key, (void*)value);
    }

    for(unsigned int i = 0; i < parser->ast->statementCount; i++) {
        ASTStatement* s = &parser->ast->statements[i];
        if(!s->isValid) continue;
        switch(s->type) {
            case AST_BLOCK_PARAMETER: analyseParameter(parser, s); break;
            case AST_BLOCK_HEADER: analyseHeader(parser, s, core); break;
            case AST_BLOCK_OPCODE: analyseOpcode(parser, s, core); break;
            case AST_BLOCK_TYPE: analyseType(parser, s); break;
            case AST_BLOCK_BITGROUP: analyseBitgroup(parser, s); break;
        }
    }
}
