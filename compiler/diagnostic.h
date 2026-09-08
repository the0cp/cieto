#ifndef CIETO_DIAGNOSTIC_H
#define CIETO_DIAGNOSTIC_H

#include "source.h"

typedef enum{
    DIAG_ERROR,
    DIAG_WARNING,
    DIAG_NOTE
}DiagSeverity;

typedef struct{
    DiagSeverity severity;
    // Strings are borrowed and remain valid for the duration of the callback.
    const char* srcName;
    const char* source;
    SourceSpan span;
    const char* message;
}Diagnostic;

typedef void (*DiagFunc)(const Diagnostic* diag, void* userData);

typedef struct{
    DiagFunc emit;
    void* userData;
}DiagSink;

#endif
