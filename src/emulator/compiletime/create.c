#include "emulator/compiletime/create.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "shared/platform.h"
#include "shared/log.h"

#define STRING_COMPONENT(x) #x,
const char* ComponentTypeNames[] = {
    FOREACH_COMPONENT(STRING_COMPONENT)
};
#undef STRING_COMPONENT

void initCore(VMCoreGen* core) {
    CONTEXT(INFO, "Creating VMCoreGen");
    ARRAY_ALLOC(Component, *core, component);
    ARRAY_ALLOC(const char*, *core, variable);
    ARRAY_ALLOC(const char*, *core, loopVariable);
    ARRAY_ALLOC(const char*, *core, command);
    ARRAY_ALLOC(Command, *core, command);
    ARRAY_ALLOC(unsigned int, *core, headBit);

    core->opcodes = NULL;
    core->opcodeCount = 0;

    TABLE2_INIT(core->headers, hashstr, cmpstr, const char*, int);
    core->codeIncludeBase = "emulator/runtime/";
    addHeader(core, "<stdbool.h>");
}

unsigned int* AllocUInt(unsigned int itemCount, ...) {
    va_list args;
    va_start(args, itemCount);

    unsigned int* data = ArenaAlloc(sizeof(unsigned int) * itemCount);

    for(unsigned int i = 0; i < itemCount; i++) {
        data[i] = va_arg(args, unsigned int);
    }

    va_end(args);

    return data;
}

Argument* AllocArgument(unsigned int itemCount, ...) {
    va_list args;
    va_start(args, itemCount);

    Argument* data = ArenaAlloc(sizeof(Argument) * itemCount);

    for(unsigned int i = 0; i < itemCount; i++) {
        data[i] = va_arg(args, Argument);
    }

    va_end(args);

    return data;
}

void addCommand(VMCoreGen* core, Command command) {
    ARRAY_PUSH(*core, command, command);
}

void addHeader(VMCoreGen* core, const char* header) {
    TABLE2_SET(core->headers, header, 1);
}

void addVariable(VMCoreGen* core, const char* format, ...) {
    va_list args;
    va_start(args, format);

    size_t len = vsnprintf(NULL, 0, format, args) + 1;
    char* buf = ArenaAlloc(len * sizeof(char));

    va_end(args);
    va_start(args, format);

    vsprintf(buf, format, args);

    va_end(args);

    ARRAY_PUSH(*core, variable, buf);
}

void addLoopVariable(VMCoreGen* core, const char* format, ...) {
    va_list args;
    va_start(args, format);

    size_t len = vsnprintf(NULL, 0, format, args) + 1;
    char* buf = ArenaAlloc(len * sizeof(char));

    va_end(args);
    va_start(args, format);

    vsprintf(buf, format, args);

    va_end(args);

    ARRAY_PUSH(*core, loopVariable, buf);
}

unsigned int addRegister(VMCoreGen* core, const char* name) {
    addHeader(core, "<stdint.h>");
    addVariable(core, "uint16_t %s", name);
    ARRAY_PUSH(*core, component, ((Component){
        .internalName = name,
        .printName = aprintf("Register %s", name),
        .type = COMPONENT_REGISTER
    }));
    return core->componentCount - 1;
}

unsigned int addBus(VMCoreGen* core, const char* name) {
    addHeader(core, "<stdint.h>");
    addVariable(core, "uint16_t %s", name);
    ARRAY_PUSH(*core, component, ((Component){
        .internalName = name,
        .printName = aprintf("Bus %s", name),
        .type = COMPONENT_BUS
    }));
    return core->componentCount - 1;
}

void addInstructionRegister(VMCoreGen* core, unsigned int iBus) {
    if(iBus >= core->componentCount) {
        cErrPrintf(TextRed, "Component %u does not exist when trying to initialise "
            "the instruction register\n", iBus);
        exit(1);
    }
    if(core->components[iBus].type != COMPONENT_BUS) {
        cErrPrintf(TextRed, "Cannot initialise instruction register using an \"%s\" "
            "component \"%s\", \"BUS\" component required\n",
            ComponentTypeNames[core->components[iBus].type], core->components[iBus].internalName);
        exit(1);
    }

    addHeader(core, "<stdint.h>");
    addVariable(core, "uint16_t opcode");
    addVariable(core, "uint16_t arg1");
    addVariable(core, "uint16_t arg2");
    addVariable(core, "uint16_t arg3");
    addVariable(core, "uint16_t arg12");
    addVariable(core, "uint16_t arg123");
    ARRAY_PUSH(*core, component, ((Component){
        .internalName = "IReg",
        .printName = "Instruction Register",
        .type = COMPONENT_OTHER
    }));
    unsigned int this = core->componentCount - 1;

    addCommand(core, (Command) {
        .name = "iRegSet",
        .file = "instRegSet",
        ARGUMENTS(((Argument){.name = "inst", .value = core->components[iBus].internalName})),
        DEPENDS(iBus),
        CHANGES(this),
        BUS_READ(iBus)
    });
}

Memory addMemory64k(VMCoreGen* core, unsigned int address, unsigned int data) {
    if(data >= core->componentCount) {
        cErrPrintf(TextRed, "Component %u does not exist when trying to initialise "
            "a 64k memory\n", data);
        exit(1);
    }
    if(core->components[data].type != COMPONENT_BUS) {
        cErrPrintf(TextRed, "Cannot initialise 64k memory using an \"%s\" "
            "component \"%i\", \"BUS\" component required\n",
            ComponentTypeNames[core->components[data].type], core->components[data].internalName);
        exit(1);
    }

    addHeader(core, "<stdint.h>");
    ARRAY_PUSH(*core, component, ((Component){
        .internalName = "Memory64",
        .printName = "Linear 64K Memory",
        .type = COMPONENT_OTHER
    }));
    unsigned int this = core->componentCount - 1;

    addCommand(core, (Command) {
        .name = aprintf("memReadTo%s", core->components[data].internalName),
        .file = "memRead",
        ARGUMENTS(
            ((Argument){.name = "data", .value = core->components[data].internalName}),
            ((Argument){.name = "address", .value = core->components[address].internalName})),
        DEPENDS(address, this),
        CHANGES(data),
        BUS_READ(address),
        BUS_WRITE(data)
    });

    addCommand(core, (Command) {
        .name = "memWrite",
        .file = "memWrite",
        ARGUMENTS(
            ((Argument){.name = "data", .value = core->components[data].internalName}),
            ((Argument){.name = "address", .value = core->components[address].internalName})),
        DEPENDS(address, data),
        CHANGES(this),
        BUS_READ(address, data)
    });

    return (Memory){
        .id = this,
        .address = address
    };
}

void addMemoryBusOutput(VMCoreGen* core, Memory* mem, unsigned int bus) {
    if(bus >= core->componentCount) {
        cErrPrintf(TextRed, "Component %u does not exist when trying to add "
            "a data output to a 64k memory\n", bus);
        exit(1);
    }
    if(core->components[bus].type != COMPONENT_BUS) {
        cErrPrintf(TextRed, "Cannot initialise additional memory output using an \"%s\" "
            "component \"%s\", \"BUS\" component required\n",
            ComponentTypeNames[core->components[bus].type], core->components[bus].internalName);
        exit(1);
    }

    addCommand(core, (Command) {
        .name = aprintf("memReadTo%s", core->components[bus].internalName),
        .file = "memRead",
        ARGUMENTS(
            ((Argument){.name = "data", .value = core->components[bus].internalName}),
            ((Argument){.name = "address", .value = core->components[mem->address].internalName})),
        DEPENDS(mem->address, mem->id),
        CHANGES(bus),
        BUS_READ(mem->address),
        BUS_WRITE(bus)
    });
}

void addBusRegisterConnection(VMCoreGen* core, unsigned int bus, unsigned int reg, int state) {
    if(bus >= core->componentCount) {
        cErrPrintf(TextRed, "Component %u does not exist when trying to connect "
            "a bus and a register\n", bus);
        exit(1);
    }

    if(reg >= core->componentCount) {
        cErrPrintf(TextRed, "Component %u does not exist when trying to connect "
            "a bus and a register\n", reg);
        exit(1);
    }
    if(core->components[bus].type != COMPONENT_BUS ||
        core->components[reg].type != COMPONENT_REGISTER) {
        cErrPrintf(TextRed, "Cannot connect bus and register using \"%s\" and \"%s\" "
            "components \"%s\" and \"%s\", \"BUS\" and \"REGISTER\" components required\n",
            ComponentTypeNames[core->components[bus].type],
            ComponentTypeNames[core->components[reg].type],
            core->components[bus].internalName, core->components[reg].internalName);
        exit(1);
    }

    if(state == -1 || state == 0) {
        addCommand(core, (Command) {
            .name = aprintf("%sTo%s", core->components[bus].internalName, core->components[reg].internalName),
            .file = "busToReg",
            ARGUMENTS(
                ((Argument){.name = "BUS", .value = core->components[bus].internalName}),
                ((Argument){.name = "REGISTER", .value = core->components[reg].internalName})),
            DEPENDS(bus),
            CHANGES(reg),
            BUS_READ(bus)
        });
    }

    if(state == 0 || state == 1) {
        addCommand(core, (Command) {
            .name = aprintf("%sTo%s", core->components[reg].internalName, core->components[bus].internalName),
            .file = "regToBus",
            ARGUMENTS(
                ((Argument){.name = "BUS", .value = core->components[bus].internalName}),
                ((Argument){.name = "REGISTER", .value = core->components[reg].internalName})),
            DEPENDS(reg),
            CHANGES(bus),
            BUS_WRITE(bus)
        });
    }
}

void addConditionRegister(VMCoreGen* core) {
    addVariable(core, "uint16_t conditions");
    addLoopVariable(core, "uint16_t currentCondition");
}

void addHaltInstruction(VMCoreGen* core) {
    addHeader(core, "<stdlib.h>");
    addHeader(core, "<stdio.h>");
    addCommand(core, (Command) {
        .name = "halt",
        .file = "halt"
    });
}
