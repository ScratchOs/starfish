#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <ctype.h>
#include "shared/arg.h"
#include "shared/platform.h"

// TODO: Add repeated arguments

// display error caused by in-correct arg-parser description
// ony caused by errors in the executable - when raised
// always a bug
static void argInternalError(argParser* parser, const char* message, ...) {
    va_list args;
    va_start(args, message);

    parser->success = false;
    cErrPrintf(TextWhite, "Implementation Error: ");
    cErrVPrintf(TextRed, message, args);
    cErrPrintf(TextWhite, "\n");

    va_end(args);
    exit(1);
}

// list of characters
typedef struct charArr {
    DEFINE_ARRAY(char, char);
} charArr;

// list of argument parsers
typedef struct argParserArr {
    DEFINE_ARRAY(argParser*, parser);
} argParserArr;

typedef struct optionArgArr {
    DEFINE_ARRAY(optionArg*, option);
} optionArgArr;

// sorting function for character list
int charSort(const void* a, const void* b) {
    const char* x = a;
    const char* y = b;

    if(*x == *y) {
        return 0;
    }
    if(isupper(*x) && !isupper(*y) && tolower(*x) == *y) {
        return -1;
    }
    if(!isupper(*x) && isupper(*y) && *y == tolower(*x)) {
        return 1;
    }
    return tolower(*x) - tolower(*y);
}

// sorting function for argument parser list
int parserSort(const void* a, const void* b) {
    const argParser* const * x = a;
    const argParser* const * y = b;
    return strcmp((*x)->name, (*y)->name);
}

// print the usage description for the current parser
static void argUsage(argParser* parser) {
    if(parser->printUsage) {
        cErrPrintf(TextWhite, "%s ", parser->name);

        charArr shortOpts;
        ARRAY_ALLOC(char, shortOpts, char);

        // gather single character options without an argument
        for(unsigned int i = 0; i < parser->options.capacity; i++) {
            Entry* entry = &parser->options.entries[i];
            if(entry->key.value == NULL) {
                continue;
            }
            optionArg* arg = entry->value;
            if(arg->type == OPT_NO_ARG && arg->hasShortName) {
                PUSH_ARRAY(char, shortOpts, char, arg->shortName);
            }
        }

        // sort the single character options
        qsort(shortOpts.chars, shortOpts.charCount, sizeof(char), charSort);

        // and print them out
        if(shortOpts.charCount > 0) {
            cErrPrintf(TextWhite, "[-%.*s] ", shortOpts.charCount, shortOpts.chars);
        }

        // print options without a single character identifier or that take an argument
        for(unsigned int i = 0; i < parser->options.capacity; i++) {
            Entry* entry = &parser->options.entries[i];
            if(entry->key.value == NULL) {
                continue;
            }
            optionArg* arg = entry->value;
            switch(arg->type) {
                case OPT_STRING:
                    if(arg->hasShortName) {
                        cErrPrintf(TextWhite, "[-%c", arg->shortName);
                    } else {
                        cErrPrintf(TextWhite, "[--%s", arg->longName);
                    }
                    cErrPrintf(TextWhite, " %s] ", arg->argumentName);
                    break;
                default:
                    if(!arg->hasShortName) {
                        cErrPrintf(TextWhite, "[--%s]", arg->longName);
                    }
                    break;
            }
        }

        // prints positional arguments
        for(unsigned int i = 0; i < parser->posArgCount; i++) {
            posArg* arg = &parser->posArgs[i];
            cErrPrintf(TextWhite, "<%s> ", arg->description);
        }

        // end of usage for current parser
        cErrPrintf(TextWhite, "\n");
    }

    argParserArr parsers;
    ARRAY_ALLOC(argParser*, parsers, parser);

    // gather all sub-parsers
    for(unsigned int i = 0; i < parser->modes.capacity; i++) {
        Entry* entry = &parser->modes.entries[i];
        if(entry->key.value == NULL) {
            continue;
        }
        PUSH_ARRAY(argParser*, parsers, parser, entry->value);
    }

    // sort the sub-parsers by name, alphabeticaly
    qsort(parsers.parsers, parsers.parserCount, sizeof(argParser*), parserSort);

    // print usage for all sub-parsers
    for(unsigned int i = 0; i < parsers.parserCount; i++) {
        argUsage(parsers.parsers[i]);
    }
}

static void optionHelp(optionArg* arg) {
    cErrPrintf(TextWhite, "  ");
    switch(arg->type) {
        case OPT_INT:
        case OPT_STRING:
            if(arg->hasShortName) {
                cErrPrintf(TextWhite, "-%c, ", arg->shortName);
            }
            cErrPrintf(TextWhite, "--%s %s\n", arg->longName, arg->argumentName);
            break;
        case OPT_NO_ARG:
            if(arg->hasShortName) {
                cErrPrintf(TextWhite, "-%c, ", arg->shortName);
            }
            cErrPrintf(TextWhite, "--%s\n", arg->longName);
    }
    cErrPrintf(TextWhite, "    %s\n", arg->helpMessage);
    arg->printed = true;
}

static void argHelp(argParser* parser) {
    cErrPrintf(TextWhite, "\n%s:\n", parser->name);

    if(parser->helpMessage != NULL) {
        cErrPrintf(TextWhite, "%s\n", parser->helpMessage);
    }

    if(parser->printUsage) {
        for(unsigned int i = 0; i < parser->posArgCount; i++) {
            posArg* arg = &parser->posArgs[i];
            cErrPrintf(TextWhite, "  <%s>\n", arg->description);
            cErrPrintf(TextWhite, "    %s\n", arg->helpMessage);
        }

        // print option help
        for(unsigned int i = 0; i < parser->options.capacity; i++) {
            Entry* entry = &parser->options.entries[i];
            if(entry->key.value == NULL) {
                continue;
            }
            optionArg* arg = entry->value;
            if(arg->isUniversal) {
                continue;
            }
            optionHelp(arg);
        }
    }

    optionArgArr universalOptions;
    ARRAY_ALLOC(optionArg*, universalOptions, option);
    for(unsigned int i = 0; i < parser->universalOptionCount; i++) {
        optionArg* arg = parser->universalOptions[i];
        if(!arg->universalChildrenOnly && (arg->universalRoot == parser || !arg->printed)) {
            PUSH_ARRAY(optionArg*, universalOptions, option, arg);
        }
    }

    if(universalOptions.optionCount > 0) {
        cErrPrintf(TextWhite, "%s; %s <*>:\n", parser->name, parser->name);
        for(unsigned int i = 0; i < universalOptions.optionCount; i++) {
            optionArg* arg = universalOptions.options[i];
            optionHelp(arg);
        }
    }

    optionArgArr childOptions;
    ARRAY_ALLOC(optionArg*, childOptions, option);
    for(unsigned int i = 0; i < parser->universalOptionCount; i++) {
        optionArg* arg = parser->universalOptions[i];
        if(arg->universalChildrenOnly && (arg->universalRoot == parser || !arg->printed)) {
            PUSH_ARRAY(optionArg*, childOptions, option, arg);
        }
    }

    if(childOptions.optionCount > 0) {
        cErrPrintf(TextWhite, "%s <*>:\n", parser->name);
        for(unsigned int i = 0; i < childOptions.optionCount; i++) {
            optionArg* arg = childOptions.options[i];
            optionHelp(arg);
        }
    }

    argParserArr parsers;
    ARRAY_ALLOC(argParser*, parsers, parser);

    // gather all sub-parsers
    for(unsigned int i = 0; i < parser->modes.capacity; i++) {
        Entry* entry = &parser->modes.entries[i];
        if(entry->key.value == NULL) {
            continue;
        }
        PUSH_ARRAY(argParser*, parsers, parser, entry->value);
    }

    // sort the sub-parsers by name, alphabeticaly
    qsort(parsers.parsers, parsers.parserCount, sizeof(argParser*), parserSort);

    // print usage for all sub-parsers
    for(unsigned int i = 0; i < parsers.parserCount; i++) {
        argHelp(parsers.parsers[i]);
    }
}

void argPrintMessage(argParser* parser) {
    if(parser->errorRoot == NULL) {
        parser->errorRoot = parser;
    }

    if(parser->helpOption->found || !parser->success) {
        cErrPrintf(TextWhite, "Usage: \n");
        argUsage(parser->errorRoot);
    }

    if(!parser->success && !parser->helpOption->found && !parser->versionOption->found) {
        for(unsigned int i = 0; i < parser->errorMessageCount; i++) {
            cErrPrintf(TextWhite, "\nError: ");
            cErrPrintf(TextRed, "%s\n", parser->errorMessages[i]);
        }
    }

    if(parser->versionOption->found) {
        cErrPrintf(TextWhite, "\n%s", parser->versionString);
    }

    if(parser->helpOption->found) {
        argHelp(parser->errorRoot);
    }

    parser->success = false;
}

// display error caused by incorrect command line
// arguments being passed by the user
static void argError(argParser* parser, const char* message, ...) {
    va_list args;
    va_start(args, message);

    parser->success = false;
    parser->errorRoot = parser;

    int len = vsnprintf(NULL, 0, message, args) + 1;
    va_end(args);
    va_start(args, message);
    char* buf = ArenaAlloc(sizeof(char) * len);
    vsprintf(buf, message, args);

    PUSH_ARRAY(const char*, *parser, errorMessage, buf);

    va_end(args);
}

void argArguments(argParser* parser, int argc, char** argv) {
    // ignore the first argument (program or mode name)
    parser->argc = argc - 1;
    parser->argv = &argv[1];
}

void argInit(argParser* parser, const char* name) {
    initTable(&parser->modes, strHash, strCmp);
    initTable(&parser->options, strHash, strCmp);
    ARRAY_ALLOC(posArg, *parser, posArg);
    ARRAY_ALLOC(const char*, *parser, errorMessage);
    ARRAY_ALLOC(optionArg*, *parser, universalOption);
    parser->success = true;
    parser->parsed = false;
    parser->parseOptions = true;
    parser->usedSubParser = false;
    parser->currentPosArg = 0;
    parser->name = name;
    parser->isSubParser = false;
    parser->errorRoot = NULL;
    parser->helpMessage = NULL;
    parser->printUsage = true;

    optionArg* help = argUniversalOption(parser, 'h', "help", false);
    help->helpMessage = "display this help message";
    parser->helpOption = help;

    optionArg* version = argUniversalOption(parser, 'V', "version", false);
    version->helpMessage = "display the version and build time of this program";
    parser->versionOption = version;
}

optionArg* argOption(argParser* parser, char shortName, const char* longName) {
    // error checking for the names
    if(tableHas(&parser->options, (void*)longName)) {
        argInternalError(parser, "Option %s already exists", longName);
    }

    if(strchr(longName, '=') != NULL) {
        argInternalError(parser, "Long names cannot contain '='");
    }
    if(strrchr(longName, ' ') != NULL) {
        argInternalError(parser, "Long names cannot contain a space character");
    }

    if(shortName == '-') {
        argInternalError(parser, "Short names cannot be '-'");
    }

    // allocate a new option
    optionArg* arg = ArenaAlloc(sizeof(optionArg));
    arg->hasShortName = shortName != '\0';
    arg->shortName = shortName;
    arg->longName = longName;
    arg->type = OPT_NO_ARG;
    arg->found = false;
    arg->value.as_string = NULL;
    arg->helpMessage = "No help message found";
    arg->printed = false;

    // add it to the table of options
    tableSet(&parser->options, (void*)longName, arg);
    return arg;
}

optionArg* argOptionString(argParser* parser, char shortName, const char* longName) {
    optionArg* arg = argOption(parser, shortName, longName);
    arg->argumentName = "string";
    arg->type = OPT_STRING;
    return arg;
}

optionArg* argOptionInt(argParser* parser, char shortName, const char* longName) {
    optionArg* arg = argOption(parser, shortName, longName);
    arg->argumentName = "number";
    arg->type = OPT_INT;
    return arg;
}

void argAddExistingOption(argParser* parser, optionArg* arg) {
    // error checking for the names
    if(tableHas(&parser->options, (void*)arg->longName)) {
        argInternalError(parser, "Option %s already exists", arg->longName);
    }

    // add it to the table of options
    tableSet(&parser->options, (void*)arg->longName, arg);
}

optionArg* argUniversalOption(argParser* parser, char shortName, const char* longName, bool childrenOnly) {
    optionArg* arg = argOption(parser, shortName, longName);
    arg->isUniversal = true;
    arg->universalRoot = parser;
    arg->universalChildrenOnly = childrenOnly;

    PUSH_ARRAY(optionArg*, *parser, universalOption, arg);
    if(childrenOnly) {
        tableRemove(&parser->options, (void*)longName);
    }

    return arg;
}

optionArg* argUniversalOptionString(argParser* parser, char shortName, const char* longName, bool childrenOnly) {
    optionArg* arg = argUniversalOption(parser, shortName, longName, childrenOnly);
    arg->type = OPT_STRING;
    arg->argumentName = "string";
    return arg;
}

optionArg* argUniversalOptionInt(argParser* parser, char shortName, const char* longName, bool childrenOnly) {
    optionArg* arg = argUniversalOption(parser, shortName, longName, childrenOnly);
    arg->type = OPT_INT;
    arg->argumentName = "number";
    return arg;
}

argParser* argMode(argParser* parser, const char* name) {
    // error checking
    if(tableHas(&parser->modes, (void*)name)) {
        argInternalError(parser, "Mode already exists");
    }

    argParser* new = ArenaAlloc(sizeof(argParser));

    // the name of a sub-parser is the main-parser's name and the sub-parsers
    // name concatenated
    int len = snprintf(NULL, 0, "%s %s", parser->name, name);
    char* nameArr = ArenaAlloc(sizeof(char) * len);
    sprintf(nameArr, "%s %s", parser->name, name);

    argInit(new, nameArr);
    new->isSubParser = true;
    new->helpOption->shortName = parser->helpOption->shortName;
    new->helpOption->longName = parser->helpOption->longName;
    new->versionOption->shortName = parser->versionOption->shortName;
    new->versionOption->longName = parser->versionOption->longName;
    ARRAY_ALLOC(optionArg*, *new, universalOption);
    for(unsigned int i = 0; i < parser->universalOptionCount; i++) {
        optionArg* arg = parser->universalOptions[i];
        PUSH_ARRAY(optionArg*, *new, universalOption, arg);
        tableSet(&new->options, (void*)arg->longName, arg);
    }

    tableSet(&parser->modes, (void*)name, new);
    return new;
}

posArg* argString(argParser* parser, const char* name) {
    posArg arg;
    arg.description = name;
    arg.type = POS_STRING;
    arg.helpMessage = "No help message found";
    PUSH_ARRAY(posArg, *parser, posArg, arg);
    return &parser->posArgs[parser->posArgCount - 1];
}

void argSetHelpMode(argParser* parser, char shortName, const char* longName) {
    parser->helpOption->shortName = shortName;
    parser->helpOption->longName = longName;
}

void argSetVersionMode(argParser* parser, char shortName, const char* longName) {
    parser->versionOption->shortName = shortName;
    parser->versionOption->longName = longName;
}

static optionArg* argFindShortName(argParser* parser, char name) {
    // loop through option table to find argument with given short name
    // table is indexed by long name only
    for(unsigned int i = 0; i < parser->options.capacity; i++) {
        Entry* entry = &parser->options.entries[i];
        if(entry->key.value == NULL) {
            continue;
        }
        optionArg* arg = entry->value;
        if(arg->shortName == name) {
            return arg;
        }
    }
    return NULL;
}

static intmax_t parseInt(argParser* parser, char* str) {
    errno = 0;
    char* endPtr;
    intmax_t ret = strtoimax(str, &endPtr, 0);
    if(errno != 0) {
        argError(parser, "Cannot parse argument as number: %s", strerror(errno));
    }
    if(*endPtr != '\0') {
        argError(parser, "Only able to parse part of argument as number");
    }
    return ret;
}

// parse argument i as a long (begins with "--") argument
static void argParseLongArg(argParser* parser, int* i) {
    optionArg* value;

    // find first equals symbol in the argument
    char* equals = strchr(&parser->argv[*i][2], '=');

    // the argument contains an equals symbol, indicating passing an argument to
    // the option this function is parsing, eg --hello=world
    if(equals != NULL) {
        // allocate a new string for the name section of the argument
        char* name = ArenaAlloc(sizeof(char) * (equals - &parser->argv[*i][2] + 1));
        strncpy(name, &parser->argv[*i][2], equals - &parser->argv[*i][2]);
        name[equals - &parser->argv[*i][2]] = '\0';

        // lookup the name of the argument
        if(tableGet(&parser->options, name, (void**)&value)) {
            switch(value->type) {
                // only type of option that takes an argument is string
                case OPT_STRING:
                    if(value->found == true) {
                        argError(parser, "Cannot repeat option \"%s\"", name);
                    }
                    // character after the equals symbol
                    value->value.as_string = equals + 1;
                    value->found = true;
                    break;
                case OPT_INT:
                    if(value->found == true) {
                        argError(parser, "Cannot repeat option \"%s\"", name);
                    }
                    value->value.as_int = parseInt(parser, equals + 1);
                    value->found = true;
                    break;
                default:
                    argError(parser, "Cannot pass argument to option \"%s\" that"
                        " does not require an argument", name);
            }
        } else {
            argError(parser, "Option \"%s\" is undefined", name);
        }
        return;
    }

    // no argument to the option within this argument
    if(tableGet(&parser->options, &parser->argv[*i][2], (void**)&value)) {
        switch(value->type) {
            // assume argument after the current one is an argument to the current option
            case OPT_STRING:
                *i += 1;
                if(*i >= parser->argc) {
                    argError(parser, "Option \"%s\" requires an argument", value->longName);
                }
                if(value->found == true) {
                    argError(parser, "Cannot repeat option \"%s\"", value->longName);
                }
                value->value.as_string = parser->argv[*i];
                value->found = true;
                break;
            case OPT_INT:
                *i += 1;
                if(*i >= parser->argc) {
                    argError(parser, "Option \"%s\" requires an argument", value->longName);
                }
                if(value->found == true) {
                    argError(parser, "Cannot repeat option \"%s\"", value->longName);
                }
                value->value.as_int = parseInt(parser, parser->argv[*i]);
                value->found = true;
                break;
            case OPT_NO_ARG:
                if(value->found == true) {
                    argError(parser, "Cannot repeat option \"%s\"", value->longName);
                }
                value->found = true;
        }
    } else {
        argError(parser, "Unknown option \"%s\"", &parser->argv[*i][2]);
    }
}

// parse option i assuming that it is a short option (eg "-a")
static void argParseShortArg(argParser* parser, int* i, unsigned int argLen) {
    optionArg* first = argFindShortName(parser, parser->argv[*i][1]);
    if(first == NULL) {
        argError(parser, "Undefined option \"%c\"", parser->argv[*i][1]);
        return;
    }

    switch(first->type) {
        case OPT_STRING:
            if(argLen > 2) {
                // argument like -llog.txt
                if(first->found == true) {
                    argError(parser, "Cannot repeat option \"%c\"", first->shortName);
                }
                first->value.as_string = &parser->argv[*i][2];
                first->found = true;
                break;
            } else {
                // argument like -l "log.txt"
                *i += 1;
                if(*i >= parser->argc) {
                    argError(parser, "Option \"%c\" requires an argument", first->shortName);
                }
                if(first->found == true) {
                    argError(parser, "Cannot repeat option \"%c\"", first->shortName);
                }
                first->value.as_string = parser->argv[*i];
                first->found = true;
                break;
            }
        case OPT_INT:
            if(argLen > 2) {
                // argument like -llog.txt
                if(first->found == true) {
                    argError(parser, "Cannot repeat option \"%c\"", first->shortName);
                }
                first->value.as_int = parseInt(parser, &parser->argv[*i][2]);
                first->found = true;
                break;
            } else {
                // argument like -l "log.txt"
                *i += 1;
                if(*i >= parser->argc) {
                    argError(parser, "Option \"%c\" requires an argument", first->shortName);
                }
                if(first->found == true) {
                    argError(parser, "Cannot repeat option \"%c\"", first->shortName);
                }
                first->value.as_int = parseInt(parser, parser->argv[*i]);
                first->found = true;
                break;
            }
        case OPT_NO_ARG:
            if(first->found == true) {
                argError(parser, "Cannot repeat option \"%c\"", first->shortName);
            }
            first->found = true;

            // argument like "-abc" where none of a, b or c have arguments
            for(unsigned int j = 2; j < argLen; j++) {
                optionArg* arg = argFindShortName(parser, parser->argv[*i][j]);
                if(arg == NULL) {
                    argError(parser, "Undefined option \"%c\"", parser->argv[*i][j]);
                    return;
                }

                switch(arg->type) {
                    case OPT_INT:
                    case OPT_STRING:
                        // do not want to support single character options with arguments
                        // inside compressed argument lists
                        argError(parser, "Option \"%c\" requires an argument so cannot be"
                            " in a multiple option argument",
                            parser->argv[*i][j]);
                        break;
                    case OPT_NO_ARG:
                        if(arg->found == true) {
                            argError(parser, "Cannot repeat option \"%c\"", arg->shortName);
                        }
                        arg->found = true;
                }
            }
    }
}

void argParse(argParser* parser) {
    // do not re-run the parser
    if(parser->parsed) {
        return;
    }

    // for all command line arguments
    for(int i = 0; i < parser->argc; i++) {

        // is the argument one of the possible modes?
        argParser* new;
        if(i == 0 && tableGet(&parser->modes, parser->argv[i], (void**)&new)) {
            // increment arguments, ignoring the mode name
            argArguments(new, parser->argc, parser->argv);
            argParse(new);

            // if sub-parser failed, propagate faliure
            parser->success &= new->success;

            // flag so errors are not repeated or flagged incorrectly
            parser->usedSubParser = true;

            parser->helpOption->found |= new->helpOption->found;
            parser->versionOption->found |= new->versionOption->found;

            for(unsigned int i = 0; i < new->errorMessageCount; i++) {
                PUSH_ARRAY(const char*, *parser, errorMessage, new->errorMessages[i]);
            }

            if(parser->errorRoot == NULL || new->helpOption->found) {
                parser->errorRoot = new->errorRoot == NULL ? new : new->errorRoot;
            }
            break;
        }

        size_t argLen = strlen(parser->argv[i]);

        // if '--' parsed, stop parsing option arguments
        if(parser->parseOptions && argLen == 2
            && parser->argv[i][0] == '-' && parser->argv[i][1] == '-') {
            parser->parseOptions = false;
            continue;
        }

        // if other argument beginning with '-' found
        if(parser->parseOptions && parser->argv[i][0] == '-' && argLen > 1) {
            if(parser->argv[i][1] == '-') {
                argParseLongArg(parser, &i);
            } else {
                argParseShortArg(parser, &i, argLen);
            }
            continue;
        }

        // parse positional argument
        if(parser->currentPosArg < parser->posArgCount) {
            posArg* arg = &parser->posArgs[parser->currentPosArg];
            switch(arg->type) {
                case POS_STRING:
                    arg->value.as_string = parser->argv[i];
            }
            parser->currentPosArg++;
            continue;
        }

        // no successfull use for the argument found
        argError(parser, "Unexpected argument \"%s\"", parser->argv[i]);
        break;
    }

    // if not all positional arguments forfilled and using this parser
    if(!parser->usedSubParser && parser->currentPosArg != parser->posArgCount) {
        posArg* arg = &parser->posArgs[parser->currentPosArg];
        argError(parser, "Missing required positional argument <%s>", arg->description);
    }
    parser->parsed = true;

    if(!parser->isSubParser && (parser->errorMessageCount > 0 || parser->helpOption->found || parser->versionOption->found)) {
        argPrintMessage(parser);
    }
}

bool argSuccess(argParser* parser) {
    return parser->success;
}
