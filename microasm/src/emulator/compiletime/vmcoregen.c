#include "emulator/compiletime/vmcoregen.h"

#include <stdio.h>
#include <stdarg.h>

void initCore(VMCoreGen* core) {
    ARRAY_ALLOC(const char*, *core, compName);
    ARRAY_ALLOC(const char*, *core, variable);
    ARRAY_ALLOC(const char*, *core, command);
    ARRAY_ALLOC(Arguments, *core, argument);
    ARRAY_ALLOC(Dependancy, *core, depends);
    ARRAY_ALLOC(Dependancy, *core, changes);
    initTable(&core->headers, strHash, strCmp);
}

void addHeader(VMCoreGen* core, const char* format, ...) {
    va_list args;
    va_start(args, format);

    size_t len = vsnprintf(NULL, 0, format, args);
    char* buf = ArenaAlloc(len * sizeof(char) + 1);
    vsnprintf(buf, len + 1, format, args);

    tableSet(&core->headers, buf, (void*)1);
}

void addVariable(VMCoreGen* core, const char* format, ...) {
    va_list args;
    va_start(args, format);

    size_t len = vsnprintf(NULL, 0, format, args);
    char* buf = ArenaAlloc(len * sizeof(char) + 1);
    vsnprintf(buf, len + 1, format, args);

    PUSH_ARRAY(const char*, *core, variable, buf);
}

Register addRegister(VMCoreGen* core, const char* name) {
    addHeader(core, "<stdint.h>");
    addVariable(core, "uint16_t %s", name);
    PUSH_ARRAY(const char*, *core, compName, name);
    return core->compNameCount - 1;
}

Bus addBus(VMCoreGen* core, const char* name) {
    addHeader(core, "<stdint.h>");
    addVariable(core, "uint16_t %s", name);
    PUSH_ARRAY(const char*, *core, compName, name);
    return core->compNameCount - 1;
}

void addInstructionRegister(VMCoreGen* core, Bus iBus) {
    addHeader(core, "<stdint.h>");
    addVariable(core, "uint16_t opcode");
    addVariable(core, "uint16_t arg1");
    addVariable(core, "uint16_t arg2");
    addVariable(core, "uint16_t arg3");
    addVariable(core, "uint16_t arg12");
    addVariable(core, "uint16_t arg123");
    PUSH_ARRAY(const char*, *core, compName, "IReg");
    unsigned int this = core->compNameCount - 1;

    {
        PUSH_ARRAY(const char*, *core, command, "emulator/runtime/instRegSet.c");
        Arguments args;
        ARRAY_ALLOC(Argument, args, arg);
        PUSH_ARRAY(Argument, args, arg, ((Argument){.name = "inst", .value = core->compNames[iBus]}));
        PUSH_ARRAY(Arguments, *core, argument, args);
        Dependancy depends;
        ARRAY_ALLOC(unsigned int, depends, dep);
        PUSH_ARRAY(unsigned int, depends, dep, iBus);
        PUSH_ARRAY(Dependancy, *core, depends, depends);
        Dependancy changes;
        ARRAY_ALLOC(unsigned int, changes, dep);
        PUSH_ARRAY(unsigned int, changes, dep, this);
        PUSH_ARRAY(Dependancy, *core, changes, changes);
    }
}

Memory addMemory64k(VMCoreGen* core, Bus address, Bus data) {
    addHeader(core, "<stdint.h>");
    PUSH_ARRAY(const char*, *core, compName, "Memory64");
    unsigned int this = core->compNameCount - 1;

    {
        PUSH_ARRAY(const char*, *core, command, "emulator/runtime/memRead.c");
        Arguments args;
        ARRAY_ALLOC(Argument, args, arg);
        PUSH_ARRAY(Argument, args, arg, ((Argument){.name = "data", .value = core->compNames[data]}));
        PUSH_ARRAY(Argument, args, arg, ((Argument){.name = "address", .value = core->compNames[address]}));
        PUSH_ARRAY(Arguments, *core, argument, args);
        Dependancy depends;
        ARRAY_ALLOC(unsigned int, depends, dep);
        PUSH_ARRAY(unsigned int, depends, dep, address);
        PUSH_ARRAY(unsigned int, depends, dep, this);
        PUSH_ARRAY(Dependancy, *core, depends, depends);
        Dependancy changes;
        ARRAY_ALLOC(unsigned int, changes, dep);
        PUSH_ARRAY(unsigned int, changes, dep, data);
        PUSH_ARRAY(Dependancy, *core, changes, changes);
    }

    {
        PUSH_ARRAY(const char*, *core, command, "emulator/runtime/memWrite.c");
        Arguments args;
        ARRAY_ALLOC(Argument, args, arg);
        PUSH_ARRAY(Argument, args, arg, ((Argument){.name = "data", .value = core->compNames[data]}));
        PUSH_ARRAY(Argument, args, arg, ((Argument){.name = "address", .value = core->compNames[address]}));
        PUSH_ARRAY(Arguments, *core, argument, args);
        Dependancy depends;
        ARRAY_ALLOC(unsigned int, depends, dep);
        PUSH_ARRAY(unsigned int, depends, dep, address);
        PUSH_ARRAY(unsigned int, depends, dep, data);
        PUSH_ARRAY(Dependancy, *core, depends, depends);
        Dependancy changes;
        ARRAY_ALLOC(unsigned int, changes, dep);
        PUSH_ARRAY(unsigned int, changes, dep, this);
        PUSH_ARRAY(Dependancy, *core, changes, changes);
    }

    return (Memory){
        .id = this,
        .address = address
    };
}

void addMemoryBusOutput(VMCoreGen* core, Memory* mem, Bus bus) {
    {
        PUSH_ARRAY(const char*, *core, command, "emulator/runtime/memRead.c");
        Arguments args;
        ARRAY_ALLOC(Argument, args, arg);
        PUSH_ARRAY(Argument, args, arg, ((Argument){.name = "data", .value = core->compNames[bus]}));
        PUSH_ARRAY(Argument, args, arg, ((Argument){.name = "address", .value = core->compNames[mem->address]}));
        PUSH_ARRAY(Arguments, *core, argument, args);
        Dependancy depends;
        ARRAY_ALLOC(unsigned int, depends, dep);
        PUSH_ARRAY(unsigned int, depends, dep, mem->address);
        PUSH_ARRAY(unsigned int, depends, dep, mem->id);
        PUSH_ARRAY(Dependancy, *core, depends, depends);
        Dependancy changes;
        ARRAY_ALLOC(unsigned int, changes, dep);
        PUSH_ARRAY(unsigned int, changes, dep, bus);
        PUSH_ARRAY(Dependancy, *core, changes, changes);
    }
}

void addBusRegisterConnection(VMCoreGen* core, Bus bus, Register reg, int state) {
    if(state == -1 || state == 0) {
        PUSH_ARRAY(const char*, *core, command, "emulator/runtime/busToReg.c");
        Arguments args;
        ARRAY_ALLOC(Argument, args, arg);
        PUSH_ARRAY(Argument, args, arg, ((Argument){.name = "BUS", .value = core->compNames[bus]}));
        PUSH_ARRAY(Argument, args, arg, ((Argument){.name = "REGISTER", .value = core->compNames[reg]}));
        PUSH_ARRAY(Arguments, *core, argument, args);
        Dependancy depends;
        ARRAY_ALLOC(unsigned int, depends, dep);
        PUSH_ARRAY(unsigned int, depends, dep, bus);
        PUSH_ARRAY(Dependancy, *core, depends, depends);
        Dependancy changes;
        ARRAY_ALLOC(unsigned int, changes, dep);
        PUSH_ARRAY(unsigned int, changes, dep, reg);
        PUSH_ARRAY(Dependancy, *core, changes, changes);
    }

    if(state == 0 || state == 1) {
        PUSH_ARRAY(const char*, *core, command, "emulator/runtime/regToBus.c");
        Arguments args;
        ARRAY_ALLOC(Argument, args, arg);
        PUSH_ARRAY(Argument, args, arg, ((Argument){.name = "BUS", .value = core->compNames[bus]}));
        PUSH_ARRAY(Argument, args, arg, ((Argument){.name = "REGISTER", .value = core->compNames[reg]}));
        PUSH_ARRAY(Arguments, *core, argument, args);
        Dependancy depends;
        ARRAY_ALLOC(unsigned int, depends, dep);
        PUSH_ARRAY(unsigned int, depends, dep, reg);
        PUSH_ARRAY(Dependancy, *core, depends, depends);
        Dependancy changes;
        ARRAY_ALLOC(unsigned int, changes, dep);
        PUSH_ARRAY(unsigned int, changes, dep, bus);
        PUSH_ARRAY(Dependancy, *core, changes, changes);
    }
}

void addHaltInstruction(VMCoreGen* core) {
    addHeader(core, "<stdlib.h>");
    addHeader(core, "<stdio.h>");
    {
        PUSH_ARRAY(const char*, *core, command, "emulator/runtime/halt.c");
        Arguments args;
        ARRAY_ALLOC(Argument, args, arg);
        PUSH_ARRAY(Arguments, *core, argument, args);
        Dependancy depends;
        ARRAY_ALLOC(unsigned int, depends, dep);
        PUSH_ARRAY(Dependancy, *core, depends, depends);
        Dependancy changes;
        ARRAY_ALLOC(unsigned int, changes, dep);
        PUSH_ARRAY(Dependancy, *core, changes, changes);
    }
}

void addCoreLoop(VMCoreGen* core, Parser* mcode) {
    addHeader(core, "<stdbool.h>");
    addHeader(core, "<stdlib.h>");
    core->opcodeCount = 1 << mcode->ast.opsize;
    core->opcodes = ArenaAlloc(sizeof(GenOpCode) * core->opcodeCount);
    for(unsigned int i = 0; i < core->opcodeCount; i++) {
        core->opcodes[i].isValid = false;
    }

    for(unsigned int i = 0; i < mcode->ast.opcodeCount; i++) {
        OpCode* code = &mcode->ast.opcodes[i];
        GenOpCode* gencode = &core->opcodes[code->id.data.value];

        gencode->isValid = true;
        gencode->name = TOKEN_GET(code->name);
        gencode->nameLen = code->name.length;

        unsigned int count = 0;
        for(unsigned int j = 0; j < code->lineCount; j++) {
            Line** line = &code->lines[j];
            count += (*line)->bits.dataCount;
        }
        gencode->bitCount = count;
        gencode->bits = ArenaAlloc(sizeof(unsigned int) * gencode->bitCount);

        unsigned int bitCounter = 0;
        for(unsigned int j = 0; j < code->lineCount; j++) {
            BitArray* line = &code->lines[j]->bits;
            for(unsigned int k = 0; k < line->dataCount; k++) {
                Token* value;
                tableGet(&mcode->ast.out.outputMap, &line->datas[k], (void**)&value);
                gencode->bits[bitCounter] = value->data.value;
                bitCounter++;
            }
        }
    }

    unsigned int count = 0;
    for(unsigned int j = 0; j < mcode->ast.head.lineCount; j++) {
        BitArray* line = &mcode->ast.head.lines[j];
        count += line->dataCount;
    }

    core->headCount = count;
    core->headBits = ArenaAlloc(sizeof(unsigned int) * count);

    unsigned int bitCounter = 0;
    for(unsigned int j = 0; j < mcode->ast.head.lineCount; j++) {
        BitArray* line = &mcode->ast.head.lines[j];
        for(unsigned int k = 0; k < line->dataCount; k++) {
            Token* value;
            tableGet(&mcode->ast.out.outputMap, &line->datas[k], (void**)&value);
            core->headBits[bitCounter] = value->data.value;
            bitCounter++;
        }
    }
}

static void outputLoop(VMCoreGen* core, FILE* file) {
    for(unsigned int i = 0; i < core->variableCount; i++) {
        fprintf(file, "%s = {0};\n", core->variables[i]);
    }

    fputs("while(true) {\n", file);

    for(unsigned int i = 0; i < core->headCount; i++) {
        unsigned int command = core->headBits[i];
        for(unsigned int k = 0; k < core->arguments[command].argCount; k++) {
            Argument* arg = &core->arguments[command].args[k];
            fprintf(file, "#define %s %s\n", arg->name, arg->value);
        }
        fprintf(file, "#include \"%s\"\n", core->commands[command]);
        for(unsigned int k = 0; k < core->arguments[command].argCount; k++) {
            Argument* arg = &core->arguments[command].args[k];
            fprintf(file, "#undef %s\n", arg->name);
        }
    }

    fputs("switch(opcode) {\n", file);

    for(unsigned int i = 0; i < core->opcodeCount; i++) {
        GenOpCode* code = &core->opcodes[i];
        if(code->isValid) {
            fprintf(file, "// %.*s\ncase %u: // %u commands\n", code->nameLen, code->name, i, code->bitCount);
            for(unsigned int j = 0; j < code->bitCount; j++) {
                unsigned int command = code->bits[j];
                for(unsigned int k = 0; k < core->arguments[command].argCount; k++) {
                    Argument* arg = &core->arguments[command].args[k];
                    fprintf(file, "#define %s %s\n", arg->name, arg->value);
                }
                fprintf(file, "#include \"%s\"\n", core->commands[command]);
                for(unsigned int k = 0; k < core->arguments[command].argCount; k++) {
                    Argument* arg = &core->arguments[command].args[k];
                    fprintf(file, "#undef %s\n", arg->name);
                }
            }
            fputs("break;\n", file);
        }
    }

    fputs("default: exit(0);\n", file);

    fputs("}\nIP++;\n}}\n", file);
}

void writeCore(VMCoreGen* core, const char* filename) {
    FILE* file = fopen(filename, "w");

    for(unsigned int i = 0; i < core->headers.capacity; i++) {
        Entry* entry = &core->headers.entries[i];
        const char* header = entry->key.value;
        if(header == NULL) {
            continue;
        }
        fprintf(file, "#include %s\n", header);
    }

    fputs("void emulator(uint16_t* memory) {\n", file);
    outputLoop(core, file);

    fputs("#define DEBUG_OUTPUT\n", file);
    fputs("void emulatorVerbose(uint16_t* memory) {\n", file);
    outputLoop(core, file);

    fclose(file);
}