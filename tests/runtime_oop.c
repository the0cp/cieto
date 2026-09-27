#include <stdio.h>
#include <string.h>

#include <cieto.h>

static bool evalRejects(
    CieVM* vm,
    const char* source,
    const char* sourceName,
    const char* expected
){
    CieStatus status = cie_vm_eval(vm, source, sourceName);
    const char* error = cie_vm_last_error(vm);
    if(status == CIE_STATUS_RUNTIME_ERROR && error != NULL &&
       strstr(error, expected) != NULL){
        return true;
    }

    fprintf(
        stderr,
        "%s was not rejected correctly: %s\n",
        sourceName,
        error != NULL ? error : cie_status_string(status)
    );
    return false;
}

static bool callReturns(CieVM* vm, const char* name, double expected){
    CieValue result;
    CieStatus status = cie_vm_call(vm, name, 0, NULL, &result);
    return status == CIE_STATUS_OK && result.type == CIE_VALUE_NUMBER &&
           result.as.number == expected;
}

int main(void){
    CieVM* vm = cie_vm_create();
    if(vm == NULL){
        fprintf(stderr, "Could not create Cieto VM.\n");
        return 1;
    }

    const char* duplicateMethod =
        "class DuplicateMethod {}\n"
        "method (d DuplicateMethod) value() { return 1; }\n"
        "var instance = DuplicateMethod();\n"
        "func currentValue() { return instance.value(); }\n"
        "method (d DuplicateMethod) value() { return 2; }\n";
    if(!evalRejects(
           vm,
           duplicateMethod,
           "duplicate_method.cies",
           "Member 'value' is already defined on class 'DuplicateMethod'."
       )){
        cie_vm_destroy(vm);
        return 1;
    }
    if(!callReturns(vm, "currentValue", 1)){
        fprintf(stderr, "Duplicate method replaced the original implementation.\n");
        cie_vm_destroy(vm);
        return 1;
    }

    const char* duplicateField =
        "class DuplicateField { Value = 1; Value = 2; }\n";
    if(!evalRejects(
           vm,
           duplicateField,
           "duplicate_field.cies",
           "Member 'Value' is already defined on class 'DuplicateField'."
       )){
        cie_vm_destroy(vm);
        return 1;
    }

    CieStatus status = cie_vm_eval(
        vm,
        "func currentField() { return DuplicateField().Value; }\n",
        "read_original_field.cies"
    );
    if(status != CIE_STATUS_OK || !callReturns(vm, "currentField", 1)){
        fprintf(stderr, "Duplicate field replaced the original default.\n");
        cie_vm_destroy(vm);
        return 1;
    }

    const char* conflictingMember =
        "class ConflictingMember { Value = 1; }\n"
        "method (c ConflictingMember) Value() { return 2; }\n";
    if(!evalRejects(
           vm,
           conflictingMember,
           "conflicting_member.cies",
           "Member 'Value' is already defined on class 'ConflictingMember'."
       )){
        cie_vm_destroy(vm);
        return 1;
    }

    cie_vm_destroy(vm);
    printf("Object semantics tests passed.\n");
    return 0;
}
