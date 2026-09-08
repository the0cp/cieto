#ifndef CIETO_SOURCE_H
#define CIETO_SOURCE_H

#include <stdint.h>

typedef struct{
    uint32_t offset;
    uint32_t line;
    uint32_t column;
}SourcePos;

typedef struct{
    SourcePos start;
    SourcePos end; // Exclusive.
}SourceSpan;

#endif
