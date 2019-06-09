#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <limits.h>
#include "platform.h"
#include "memory.h"

const char* resolvePath(const char* path) {
#ifdef _WIN32
    // _fullpath is defined in microsoft's stdlib.h
    return _fullpath(NULL, path, _MAX_PATH);
#else
    char* buf = malloc(PATH_MAX + 1);
    char* ret = realpath(path, buf);
    if(ret) {
        return buf;
    } else {
        return path;
    }
#endif
}

bool EnableColor = false;

void startColor() {
    EnableColor = true;
#ifdef _WIN32
    HandleErr = GetStdHandle(STD_ERROR_HANDLE);
    HandleOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if(!GetConsoleScreenBufferInfo(HandleErr, &ErrReset) || 
       !GetConsoleScreenBufferInfo(HandleOut, &OutReset)) {
        EnableColor = false;
    }
#endif
}

void cErrPrintf(TextColor color, const char* format, ...) {
    va_list args;
    va_start(args, format);

    cErrVPrintf(color, format, args);
}

void cErrVPrintf(TextColor color, const char* format, va_list args) {
#ifdef _WIN32
    if(EnableColor) SetConsoleTextAttribute(HandleErr, color | FOREGROUND_INTENSITY);
    vfprintf(stderr, format, args);
    if(EnableColor) SetConsoleTextAttribute(HandleErr, ErrReset.wAttributes);
#else
    if(EnableColor) fprintf(stderr, "\x1B[1;%um", color);
    vfprintf(stderr, format, args);
    if(EnableColor) fprintf(stderr, "\x1B[0m");
#endif
}

void cOutPrintf(TextColor color, const char* format, ...) {
    va_list args;
    va_start(args, format);

    cOutVPrintf(color, format, args);
}

void cOutVPrintf(TextColor color, const char* format, va_list args) {
#ifdef _WIN32
    if(EnableColor) SetConsoleTextAttribute(HandleOut, color | FOREGROUND_INTENSITY);
    vfprintf(stdout, format, args);
    if(EnableColor) SetConsoleTextAttribute(HandleOut, OutReset.wAttributes);
#else
    if(EnableColor) fprintf(stdout, "\x1B[1;%um", color);
    vfprintf(stdout, format, args);
    if(EnableColor) fprintf(stdout, "\x1B[0m");
#endif
}

// get a buffer containing the string contents of the file provided
const char* readFile(const char* fileName) {
    FILE* file = fopen(fileName, "r");
    if(file == NULL){
        printf("Could not read file \"%s\"\n", fileName);
        exit(1);
    }
    
    // get the length of the file
    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    // +1 so '\0' can be added
    // buffer should stay allocated for lifetime 
    // of compiler as all tokens reference it
    char* buffer = (char*)ArenaAlloc((fileSize + 1) * sizeof(char));
    if(buffer == NULL){
        printf("Could not enough allocate memory to read file \"%s\".\n", fileName);
        exit(1);
    }
    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    buffer[bytesRead] = '\0';

    fclose(file);

    return buffer;
}
