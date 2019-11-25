#include "microcode/analyse.h"

#include <math.h>
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
// TODO finish opcode analysis, parameters, paramatised bits

// User defined types

typedef struct IdentifierEnum {
    Token* definition;

    // null terminated strings for each name in the enum
    DEFINE_ARRAY(char*, member);

    // maximum length of each member name
    unsigned int identifierLength;

    // those names, but in a hash map
    Table membersTable;
} IdentifierEnum;

// for error formatting
char* UserTypeNames[] = {
    [USER_TYPE_ANY] = "any",
    [USER_TYPE_ENUM] = "enum"
};


// Base types

// parameter, eg "phase: 4"
typedef struct IdentifierParameter {
    // name of the parameter
    Token* definition;

    unsigned int value;
} IdentifierParameter;

// bit defined by structure of VM, not found in code
typedef struct IdentifierControlBit {
    unsigned int value;
} IdentifierControlBit;

// user defined type
typedef struct IdentifierUserType {
    UserType type;
    union {
        IdentifierEnum enumType;
    } as;
} IdentifierUserType;

// group of control bits
typedef struct IdentifierBitGroup {
    Token* definition;

    // maximum length of all identifiers in group + null byte
    unsigned int lineLength;

    // list of null terminated strings of length lineLength
    char* substitutedIdentifiers;
} IdentifierBitGroup;

// all possible types
typedef enum IdentifierType {
    TYPE_PARAMETER,
    TYPE_VM_CONTROL_BIT,
    TYPE_USER_TYPE,
    TYPE_BITGROUP
} IdentifierType;

// for error formatting
char *IdentifierTypeNames[] = {
    [TYPE_PARAMETER] = "parameter",
    [TYPE_VM_CONTROL_BIT] = "vm control bit",
    [TYPE_USER_TYPE] = "user type",
    [TYPE_BITGROUP] = "bitgroup"
};

// so a name can be mapped to a type
typedef struct Identifier {
    IdentifierType type;
    union {
        IdentifierParameter parameter;
        IdentifierControlBit control;
        IdentifierUserType userType;
        IdentifierBitGroup bitgroup;
    } as;
} Identifier;


// why does c not define this in math.h?
static int max(int a, int b) {
    return a > b ? a : b;
}

// table mapping names to types, probably shouldnt be a global - but is.  Sorry.
Table identifiers;

Table erroredParameters;
static Error errParamMissing = {0};
static Error errParameterWrongType = {0};
static void getParameterErrors() {
    initTable(&erroredParameters, strHash, strCmp);
    newErrAt(&errParamMissing, ERROR_SEMANTIC, "Parameter '%s' required to "
        "parse %s not found");
    newErrAt(&errParameterWrongType, ERROR_SEMANTIC, "To parse %s, '%s' is "
        "required as a parameter, but it is defined as a %s");
}

// lookup a name as an identifier, check
//   if the name exists
//   if it represents a parameter
// if either check fails, emit error
//   unless error already emitted about that name
static Identifier* getParameter(Parser* parser, Token* errPoint, char* name,
    char* usage) {
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
    error(parser, &errParameterWrongType, errPoint, usage, name,
        IdentifierTypeNames[value->type]);
    tableSet(&erroredParameters, name, (void*)1);
    return NULL;
}

static Error errDuplicateDefine = {0};
static void alreadyDefinedErrors() {
    newErrAt(&errDuplicateDefine, ERROR_SEMANTIC, "One or more prior "
    "definitions for '%s' found, currently declared as being of type %s");
}

// report symbol redefenition error at correct location
static void alreadyDefined(Parser* parser, char* name, Identifier* current,
    ASTStatement* s) {
    Token* errLoc;
    switch(s->type) {
        case AST_BLOCK_BITGROUP: errLoc = &s->as.bitGroup.name; break;
        case AST_BLOCK_HEADER: errLoc = &s->as.header.errorPoint; break;
        case AST_BLOCK_OPCODE: errLoc = &s->as.opcode.name; break;
        case AST_BLOCK_PARAMETER: errLoc = &s->as.parameter.name; break;
        case AST_BLOCK_TYPE: errLoc = &s->as.type.name; break;
    }

    error(parser, &errDuplicateDefine, errLoc, name,
        IdentifierTypeNames[current->type]);
}

static Error errWrongType = {0};
static void wrongTypeErrors() {
    newErrAt(&errWrongType, ERROR_SEMANTIC, "Expecting identifier '%s' to have "
        "type '%s', got type '%s'");
}

// wrapper to make reporting type errors easier
static void wrongType(Parser* parser, Token* errLoc, IdentifierType expected,
    Identifier* val) {
    error(parser, &errWrongType, errLoc, errLoc->data.string,
        IdentifierTypeNames[expected], IdentifierTypeNames[val->type]);
}

static Error errUndefinedType = {0};
static Error errNotType = {0};
static Error errWrongUserType = {0};
static void userTypeCheckErrors() {
    newErrAt(&errUndefinedType, ERROR_SEMANTIC, "Identifier '%s' is not"
    "defined, %s type expected");
    newErrAt(&errNotType, ERROR_SEMANTIC, "Expected identifier to be a type, "
        "but got %s");
    newErrAt(&errWrongUserType, ERROR_SEMANTIC, "Expected identifier to have "
        "user type of '%s' however, it has type '%s'");
}

// check if a type name has the required user defined type
// if not, report an error
static bool userTypeCheck(Parser* parser, UserType typeRequired,
    ASTParameter *typePair) {
    Identifier* ident;
    if(!tableGet(&identifiers, (char*)typePair->name.data.string,
                 (void**)&ident)) {
        error(parser, &errUndefinedType, &typePair->name,
            typePair->name.data.string, UserTypeNames[typeRequired]);
        return false;
    }

    if(ident->type != TYPE_USER_TYPE) {
        wrongType(parser, &typePair->name, TYPE_USER_TYPE, ident);
        return false;
    }

    if(typeRequired == USER_TYPE_ANY) {
        return true;
    }

    if(ident->as.userType.type != typeRequired) {
        error(parser, &errWrongUserType, UserTypeNames[typeRequired],
            UserTypeNames[ident->as.userType.type]);
    }

    return true;
}

// check a parameter and add it to the identifiers map
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
    newErrAt(&errNoOrdering, ERROR_SEMANTIC, "Unable to order microcode bits "
        "in line %u");
    newErrAt(&errBusRead, ERROR_SEMANTIC, "Command reads from bus before it "
        "was written in line %u");
}

// analyse an array of microcode bits
// assumes that all the identifiers in the array exist and have the correct type
static NodeArray analyseLine(VMCoreGen* core, Parser* mcode, BitArray* line,
    Token* opcodeName, unsigned int lineNumber) {
    CONTEXT(INFO, "Analysing line");

    Graph graph;
    InitGraph(&graph);

    // This works. Dont ask how, I cannot remember
    // creates dependancy graph for all the microcode bits, based on what they
    // change and what values they depend on.
    for(unsigned int i = 0; i < line->dataCount; i++) {
        Identifier* value;
        tableGet(&identifiers, (char*)line->datas[i].data.data.string,
            (void**)&value);
        unsigned int command = value->as.control.value;
        AddNode(&graph, command);
        for(unsigned int j = 0;
            j < core->commands[command].changesLength; j++) {
            unsigned int changed = core->commands[command].changes[j];
            for(unsigned int k = 0; k < line->dataCount; k++) {
                Identifier* value;
                tableGet(&identifiers, (char*)line->datas[k].data.data.string,
                    (void**)&value);
                unsigned int comm = value->as.control.value;
                for(unsigned int l = 0;
                    l < core->commands[comm].dependsLength; l++) {
                    unsigned int depended = core->commands[comm].depends[l];
                    if(changed == depended) {
                        AddEdge(&graph, command, comm);
                    }
                }
            }
        }
    }

    // get execution order for the microcode bits
    NodeArray nodes = TopologicalSort(&graph);
    if(!nodes.validArray) {
        error(mcode, &errNoOrdering, opcodeName, lineNumber);
    }

    // checking if any bus reads happen when the bus has not been written to

    // set all busses to not set
    for(unsigned int i = 0; i < core->componentCount; i++) {
        Component* component = &core->components[i];
        component->busStatus = false;
    }

    // loop through all commands in execution order
    for(unsigned int i = 0; i < nodes.nodeCount; i++) {
        Command* command = &core->commands[nodes.nodes[i]->value];

        // read from bus and check if possible
        for(unsigned int j = 0; j < command->readsLength; j++) {
            Component* bus = &core->components[command->reads[j]];
            if(!bus->busStatus) {
                error(mcode, &errBusRead, opcodeName, lineNumber);
            }
        }

        // write to bus, allow it to be read from
        // todo - do not allow multiple writes to a bus
        for(unsigned int j = 0; j < command->writesLength; j++) {
            Component* bus = &core->components[command->writes[j]];
            bus->busStatus = true;
        }
    }

    return nodes;
}

static Error errIdentifierNotDefined = {0};
static void mcodeBitArrayCheckErrors() {
    newErrAt(&errIdentifierNotDefined, ERROR_SEMANTIC,
        "Identifier was not defined");
}

// check if all identifers in the array reperesent a control bit
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
        if(val->type != TYPE_VM_CONTROL_BIT) {
            wrongType(parser, bit, TYPE_VM_CONTROL_BIT, val);
            passed = false;
        }
    }

    return passed;
}

static Error errDuplicateHeader = {0};
static Error errHeaderLineCount = {0};
static void analyseHeaderErrors() {
    newErrAt(&errDuplicateHeader, ERROR_SEMANTIC,
        "Cannot have more than one header statement in a microcode");
    newErrNoteAt(&errDuplicateHeader, "Header first included here");
    newErrAt(&errHeaderLineCount, ERROR_SEMANTIC,
        "Number of lines in header (%u) is too high, the maximum is %u");
}

// has a header statement been analysed yet?
static bool parsedHeader = false;

// the header statement AST, used for emitting duplicate header errors
static ASTStatement* firstHeader = NULL;

// check a header statement and put results into codegen
static void analyseHeader(Parser* parser, ASTStatement* s, VMCoreGen* core) {
    CONTEXT(INFO, "Analysing header");

    if(parsedHeader) {
        error(parser, &errDuplicateHeader, &s->as.header.errorPoint,
            firstHeader->as.header.errorPoint);
        return;
    }
    parsedHeader = true;
    firstHeader = s;

    Identifier* phase = getParameter(parser, &s->as.header.errorPoint,
        "phase", "header");
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

        NodeArray nodes = analyseLine(core, parser, line,
            &s->as.header.errorPoint, i);
        for(unsigned int j = 0; j < nodes.nodeCount; j++) {
            PUSH_ARRAY(unsigned int, *core, headBit, nodes.nodes[j]->value);
        }
    }
}

static Error errOpcodeIdSize = {0};
static Error errOpcodeLineCount = {0};
static void analyseOpcodeErrors() {
    newErrAt(&errOpcodeIdSize, ERROR_SEMANTIC, "Opcode id is too large");
    newErrAt(&errOpcodeLineCount, ERROR_SEMANTIC, "Number of lines in opcode "
        "is too high");
}

static void analyseOpcode(Parser* parser, ASTStatement* s, VMCoreGen* core) {
    CONTEXT(INFO, "Analysing opcode statement");

    if(!parsedHeader) return;
    Identifier* phase = getParameter(parser, &s->as.opcode.name,
        "phase", "opcode");
    if(phase == NULL) return;
    unsigned int maxLines = (1 << phase->as.parameter.value) -
        firstHeader->as.parameter.value.data.value;

    Identifier* opsize = getParameter(parser, &s->as.opcode.name,
        "opsize", "opcode");
    if(opsize == NULL) return;
    unsigned int maxOpCodes = (1 << opsize->as.parameter.value);

    GenOpCode gencode;
    ARRAY_ALLOC(GenOpCodeLine*, gencode, line);

    // basic header checking
    // TODO: needs improving, check parameters
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

        // by default analyse low bits
        // are all of the bits valid
        if(!mcodeBitArrayCheck(parser, &line->bitsLow)) {
            // if not, analyseLine would crash, so skip this line
            continue;
        }
        NodeArray lowNodes = analyseLine(core, parser, &line->bitsLow,
            &s->as.opcode.name, j);
        ARRAY_ALLOC(unsigned int, *genline, lowBit);
        for(unsigned int k = 0; k < lowNodes.nodeCount; k++) {
            PUSH_ARRAY(unsigned int, *genline, lowBit,
                lowNodes.nodes[k]->value);
        }

        if(line->hasCondition) {
            // only check high bits if there is a condition,
            // otherwise they are identical
            if(!mcodeBitArrayCheck(parser, &line->bitsHigh)) {
                continue;
            }
            NodeArray highNodes = analyseLine(core, parser, &line->bitsHigh,
                &s->as.opcode.name, j);
            ARRAY_ALLOC(unsigned int, *genline, highBit);
            for(unsigned int k = 0; k < highNodes.nodeCount; k++) {
                PUSH_ARRAY(unsigned int, *genline, highBit,
                    highNodes.nodes[k]->value);
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
    newErrAt(&errEnumMoreMembers, ERROR_SEMANTIC, "Enum statement requires %u "
        "members, got %u");
    newErrAt(&errEnumLessMembers, ERROR_SEMANTIC, "Enum statement requires %u "
        "members, got %u");
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
    value->type = TYPE_USER_TYPE;
    value->as.userType.type = USER_TYPE_ENUM;
    IdentifierEnum* enumIdent = &value->as.userType.as.enumType;
    enumIdent->definition = &s->as.type.name;
    tableSet(&identifiers, (char*)type->data.string, (void*)value);

    // check the correct number of members are present
    unsigned int size = enumStatement->width.data.value;
    unsigned int requiredMemberCount = size == 1 ? 2 : 1 << size;
    if(enumStatement->memberCount != requiredMemberCount) {
        if(enumStatement->memberCount < requiredMemberCount) {
            error(parser, &errEnumMoreMembers, &typeStatement->name,
                requiredMemberCount, enumStatement->memberCount);
        } else {
            error(parser, &errEnumLessMembers,
                &enumStatement->members[requiredMemberCount],
                requiredMemberCount, enumStatement->memberCount);
        }
    }

    // as enums cannot be used directly, no issues if
    // enum values collide with other identifiers

    Table membersTable;
    initTable(&membersTable, tokenHash, tokenCmp);
    ARRAY_ALLOC(char*, *enumIdent, member);
    enumIdent->identifierLength = 0;

    // check there are no duplicated names
    for(unsigned int i = 0; i < enumStatement->memberCount; i++) {
        Token* tok = &enumStatement->members[i];
        if(tableHas(&membersTable, tok)) {
            Token* original;
            tableGetKey(&membersTable, tok, (void**)&original);
            error(parser, &errEnumDuplicate, tok, original);
        } else {
            tableSet(&membersTable, tok, (void*)i);
            PUSH_ARRAY(char*, *enumIdent, member, (char*)tok->data.string);
            enumIdent->identifierLength =
                max(enumIdent->identifierLength, tok->length);
        }
    }

    enumIdent->membersTable = membersTable;
}

static void analyseType(Parser* parser, ASTStatement* s) {
    CONTEXT(INFO, "Analysing type statement");

    switch(s->as.type.type) {
        case USER_TYPE_ENUM: analyseEnum(parser, s); break;
        case USER_TYPE_ANY:
            // Unreachable - should not be able to construct an any type
            // in the parser, only used for analysis
            break;
    }
}

static Error errBitgroupParamSelfShadow = {0};
static Error errBitgroupSubsUndefined = {0};
static Error errSubstitutionError = {0};
static Error errSubstitutionType = {0};
static void analyseBitgroupErrors() {
    newErrAt(&errBitgroupParamSelfShadow, ERROR_SEMANTIC, "Parameter name '%s' "
        "collides with another parameter of the same name");
    newErrAt(&errBitgroupSubsUndefined, ERROR_SEMANTIC, "Variable to "
        "substitute is not defined");
    newErrAt(&errSubstitutionError, ERROR_SEMANTIC, "Found undefined resultant "
        "identifier while substituting into bitgroup");
    newErrAt(&errSubstitutionType, ERROR_SEMANTIC, "Found resultant identifier "
        "to have type %s, expecting VM_CONTROL_BIT while substituting "
        "into bitgroup");
}

static void analyseBitgroup(Parser* parser, ASTStatement* s) {
    CONTEXT(INFO, "Analysing type statement");

    Identifier* value;
    if(tableGet(&identifiers, (char*)s->as.bitGroup.name.data.string,
                (void**)&value)) {
        alreadyDefined(parser, (char*)s->as.bitGroup.name.data.string,
            value, s);
        return;
    }

    value = ArenaAlloc(sizeof(Identifier));
    value->type = TYPE_BITGROUP;
    value->as.bitgroup.definition = &s->as.type.name;
    tableSet(&identifiers, (char*)s->as.type.name.data.string, (void*)value);

    Table paramNames;
    initTable(&paramNames, tokenHash, tokenCmp);

    bool passed = true;

    // check that all parameters are valid enums and that the assigned names
    // are not duplicated
    for(unsigned int i = 0; i < s->as.bitGroup.paramCount; i++) {
        ASTParameter* pair = &s->as.bitGroup.params[i];
        passed &= userTypeCheck(parser, USER_TYPE_ENUM, pair);

        if(tableHas(&paramNames, &pair->value)) {
            error(parser, &errBitgroupParamSelfShadow, &pair->value,
                pair->value.data.string);
        }
        tableSet(&paramNames, &pair->value, &pair->name);
    }
    if(!passed) {
        return;
    }


    // calculate the maximum length buffer required to store each possible
    // permutation of inupts
    unsigned int lineLength = 1; // starts at 1 due to null terminater
    unsigned int possibilities = 1;
    unsigned int substitutions = 0;
    for(unsigned int i = 0; i < s->as.bitGroup.segmentCount; i++) {
        ASTBitGroupIdentifier* seg = &s->as.bitGroup.segments[i];
        if(seg->type == AST_BIT_GROUP_IDENTIFIER_SUBST) {
            Token* typeName;
            if(!tableGet(&paramNames, &seg->identifier, (void**)&typeName)) {
                error(parser, &errBitgroupSubsUndefined, &seg->identifier);
                passed = false;
            } else {
                Identifier* type;
                tableGet(&identifiers, (char*)typeName->data.string,
                    (void**)&type);
                IdentifierEnum* enumType = &type->as.userType.as.enumType;
                lineLength += enumType->identifierLength;
                possibilities *= enumType->memberCount;
                substitutions++;
            }
        } else {
            lineLength += seg->identifier.length;
        }
    }
    if(!passed) {
        return;
    }

    // algorithm based off of fullfact from MATLAB's stats toolkit,
    // allows an int to be converted into a value to substitute
    unsigned int* nreps = ArenaAlloc(sizeof(unsigned int)*substitutions);
    unsigned int* ncycles = ArenaAlloc(sizeof(unsigned int)*substitutions);
    unsigned int count = 0;
    for(unsigned int i = 0; i < s->as.bitGroup.segmentCount; i++){
        ASTBitGroupIdentifier* seg = &s->as.bitGroup.segments[i];
        if(seg->type == AST_BIT_GROUP_IDENTIFIER_SUBST) {
            Token* typeName;
            tableGet(&paramNames, &seg->identifier, (void**)&typeName);
            Identifier* type;
            tableGet(&identifiers, (char*)typeName->data.string, (void**)&type);
            unsigned int memberCount =
                type->as.userType.as.enumType.memberCount;
            if(count == 0) {
                nreps[count] = 1;
                ncycles[count] = possibilities/memberCount;
            } else {
                nreps[count] = possibilities/ncycles[count-1];
                ncycles[count] = ncycles[count-1]/memberCount;
            }
            count += 1;
        }
    }

    // list of null terminated strings
    char* substitutedList =
        ArenaAlloc(sizeof(char) * lineLength * possibilities);
    for(unsigned int i = 0; i < possibilities; i++) {
        char* currentIdent = &substitutedList[i*lineLength];
        currentIdent[0] = '\0';
        for(unsigned int j = 0; j < s->as.bitGroup.segmentCount; j++){
            ASTBitGroupIdentifier* seg = &s->as.bitGroup.segments[j];
            if(seg->type == AST_BIT_GROUP_IDENTIFIER_SUBST) {
                // type varified to be valid earlier in function
                Token* typeName;
                tableGet(&paramNames, &seg->identifier, (void**)&typeName);
                Identifier* type;
                tableGet(&identifiers, (char*)typeName->data.string,
                    (void**)&type);
                IdentifierEnum* enumIdent = &type->as.userType.as.enumType;

                // Played around in excel to work this out, cannot remember
                // how it works, but it does.  I don't think I knew when
                // I wrote it either.
                strcat(currentIdent, enumIdent->members[(int)floor(((double)i -
                    floor((double)i/((double)enumIdent->memberCount *
                    (double)nreps[i]))* (double)enumIdent->memberCount *
                    (double)nreps[i]) / (double)nreps[i])]);
            } else {
                strncat(currentIdent, seg->identifier.data.string,
                    seg->identifier.length);
            }
        }

        // check that after the substitutions have completed, a valid
        // control bit was formed
        Identifier* val;
        if(!tableGet(&identifiers, currentIdent, (void**)&val)) {
            error(parser, &errSubstitutionError, value->as.bitgroup.definition);
            passed = false;
        }
        if(val->type != TYPE_VM_CONTROL_BIT) {
            error(parser, &errSubstitutionType, value->as.bitgroup.definition,
                IdentifierTypeNames[val->type]);
            passed = false;
        }

        if(!passed) {
            return;
        }
    }

    value->as.bitgroup.substitutedIdentifiers = substitutedList;
    value->as.bitgroup.lineLength = lineLength;
}

static bool errorsInitialised;
typedef void (*errorInitialiser)();
static errorInitialiser errorInitialisers[] = {
    getParameterErrors,
    alreadyDefinedErrors,
    wrongTypeErrors,
    userTypeCheckErrors,
    analyseLineErrors,
    mcodeBitArrayCheckErrors,
    analyseHeaderErrors,
    analyseOpcodeErrors,
    analyseTypeErrors,
    analyseBitgroupErrors
};

void InitAnalysis() {
    // initialise errors in consistant order
    // can be run without the analysis to be able to map an error id
    // to an error
    if(!errorsInitialised) {
        for(unsigned int i = 0;
            i < sizeof(errorInitialisers)/sizeof(errorInitialiser); i++) {
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
