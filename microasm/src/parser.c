#include <stdio.h>
#include <stdlib.h>
#include "token.h"
#include "parser.h"
#include "ast.h"
#include "memory.h"
#include "platform.h"

static void advance(Parser* parser);
static bool match(Parser* parser, OrangeTokenType type);
static bool check(Parser* parser, OrangeTokenType type);
static void consume(Parser* parser, OrangeTokenType type, const char* message);
static void syncronise(Parser* parser);
static void block(Parser* parser);
static void header(Parser* parser, bool write);
static Line* microcodeLine(Parser* parser);
static bool blockStart(Parser* parser);
static void blockEnd(Parser* parser, bool start);
static void errorAtCurrent(Parser* parser, const char* message);
static void error(Parser* parser, const char* message);
static void errorAt(Parser* parser, Token* token, const char* message);

void ParserInit(Parser* parser, Scanner* scan) {
    parser->scanner = scan;
    parser->hadError = false;
    parser->panicMode = false;
    parser->headerStatement = false;
    parser->inputStatement = false;
    parser->outputStatement = false;
    InitMicrocode(&parser->ast, scan->fileName);
}

bool Parse(Parser* parser) {
    // gets first token, required so not matching garbage(causes segfault)
    advance(parser);

    while(!match(parser, TOKEN_EOF)){
        // all file-level constructs are blocks of some form
        block(parser);
    }

    return !parser->hadError;
}

// reports all error tokens, returning next non error token
static void advance(Parser* parser) {
    parser->previous = parser->current;

    for(;;) {
        parser->current = ScanToken(parser->scanner);
        if(parser->current.type != TOKEN_ERROR){
            break;
        }
        errorAtCurrent(parser, parser->current.start);
    }
}

// consume a token of type type, else return false
static bool match(Parser* parser, OrangeTokenType type) {
    if(!check(parser, type)) return false;
    advance(parser);
    return true;
}

static void consume(Parser* parser, OrangeTokenType type, const char* message) {
    if(parser->current.type == type) {
        advance(parser);
        return;
    }
    errorAtCurrent(parser, message);
}

// is the next token of type type?
static bool check(Parser* parser, OrangeTokenType type) {
    return parser->current.type == type;
}

// get to known parser state after error occured
static void syncronise(Parser* parser) {
    parser->panicMode = false;
    while(parser->current.type != TOKEN_EOF) {
        switch(parser->current.type) {
            // should mostly be able to continue parsing from these tokens
            case TOKEN_INPUT:
            case TOKEN_OPCODE:
            case TOKEN_HEADER:
            case TOKEN_OUTPUT:
            case TOKEN_MACRO:
                return;
            default:;  // do nothing - cannot calculate a known parser state
        }
        advance(parser);
    }
}

// dispatch the parser for a block level statement
static void block(Parser* parser) {
    if(match(parser, TOKEN_OPCODE)) {
        //TODO
    } else if(match(parser, TOKEN_MACRO)) {
        //TODO
    } else if(match(parser, TOKEN_HEADER)) {
        if(parser->headerStatement){
            error(parser, "Only one header statement allowed per microcode");
        }
        header(parser, !parser->headerStatement);
        parser->headerStatement = true;
    } else if(match(parser, TOKEN_INPUT)) {
        if(parser->inputStatement){
            error(parser, "Only one input statement allowed per microcode");
        }
        parser->inputStatement = true;
        //TODO
    } else if(match(parser, TOKEN_OUTPUT)) {
        if(parser->outputStatement){
            error(parser, "Only one output statement allowed per microcode");
        }
        parser->outputStatement = true;
        //TODO
    } else {
        errorAtCurrent(parser, "Expected a block statement");
    }

    // if error occured reset parser state to known value
    if(parser->panicMode) syncronise(parser);
}

// parses a header statement
static void header(Parser* parser, bool write) {
    bool brace = blockStart(parser);
    Line* line = microcodeLine(parser);
    if(line->conditionCount != 0) {
        errorAt(parser, &line->condition1Equals, "Condition values not allowed in header");
    }
    if(write) {
        parser->ast.head.bits = line->bits;
        parser->ast.head.bitCount = line->bitCount;
        parser->ast.head.bitCapacity = line->bitCapacity;
    }
    blockEnd(parser, brace);
}

// parses a line of microcode commands with conditions
// returns the line ast representing what was parsed
static Line* microcodeLine(Parser* parser) {
    // are conditions being parsed?
    bool cond = true;

    // is this the first parse iteration?
    bool first = true;

    // testing code
    Line* line = ArenaAlloc(&parser->ast.arena, sizeof(Line));
    line->bitCount = 0;
    line->bitCapacity = 8;
    line->bits = ArenaAlloc(&parser->ast.arena, sizeof(Token)*line->bitCapacity);
    line->conditionCount = 0;
    line->conditionCapacity = 8;
    line->conditions = ArenaAlloc(&parser->ast.arena, sizeof(Condition)*line->conditionCapacity);
    line->condition1Equals = (Token){.start = NULL};

    for(;;) {
        consume(parser, TOKEN_IDENTIFIER, "Expected identifier");
        Token name = parser->previous;
        if(first) { // in first loop iteration
            cond = check(parser, TOKEN_EQUAL);
        }
        if(cond) {
            consume(parser, TOKEN_EQUAL, "Expected equals symbol");

            if(first) {
                line->condition1Equals = parser->previous;
            }

            consume(parser, TOKEN_IDENTIFIER, "Expected condition value");

            Condition condition;
            condition.name = name;
            condition.value = parser->previous;
            if(line->conditionCapacity > 0) {
                line->conditions[line->conditionCount] = condition;
                line->conditionCount++;
                line->conditionCapacity--;
            }
        } else {
            if(line->bitCapacity > 0) {
                line->bits[line->bitCount] = name;
                line->bitCount++;
                line->bitCapacity--;
            }
        }

        // are there more values to parse?
        if(!match(parser, TOKEN_COMMA)) {
            break;
        }
        first = false;
    }

    if(cond) {
        // seperator between conditions and bit names required
        consume(parser, TOKEN_COLON, "Expected colon");
    } else {
        // no conditions so second loop not required
        return line;
    }

    // bits
    for(;;) {
        consume(parser, TOKEN_IDENTIFIER, "Expected bit name");
        if(line->bitCapacity > 0) {
            line->bits[line->bitCount] = parser->previous;
            line->bitCount++;
            line->bitCapacity--;
        }
        if(!match(parser, TOKEN_COMMA)) {
            break;
        }
    }

    return line;
}

// parse a single or multi-line block start and return the type parsed
// true = multi-line, false = single-line
static bool blockStart(Parser* parser) {
    if(match(parser, TOKEN_LEFT_BRACE)) {
        return true;  // multiline block
    } else if(match(parser, TOKEN_EQUAL)) {
        return false; // single line block
    }
    errorAtCurrent(parser, "Expected the start of a block");
    return false; // value does not matter
}

// expect the ending of a block based on the start type
static void blockEnd(Parser* parser, bool start) {
    if(start) {
        // optional semicolon
        match(parser, TOKEN_SEMICOLON);
        consume(parser, TOKEN_RIGHT_BRACE, "Expected a right brace at end of block");
    } else {
        consume(parser, TOKEN_SEMICOLON, "Expected a semi-colon at end of block");
    }
}

// issue error for token before advance() called
static void errorAtCurrent(Parser* parser, const char* message) {
    errorAt(parser, &parser->current, message);
}

// issue error for already advanced() token
static void error(Parser* parser, const char* message) {
    errorAt(parser, &parser->previous, message);
}

// print an error message at a token's position
static void errorAt(Parser* parser, Token* token, const char* message) {
    if(parser->panicMode) return;
    parser->panicMode = true;

    fprintf(stderr, "[%i:%i] ", token->line, token->column);
    cErrPrintf(TextRed, "Error");
    if(token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if(token->type == TOKEN_ERROR) {
        // nothing
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser->hadError = true;
    parser->ast.hasError = true;
}