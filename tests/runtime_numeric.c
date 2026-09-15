#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include <cieto.h>

static int reportCallFailure(CieVM* vm, const char* name, CieStatus status){
    const char* error = cie_vm_last_error(vm);
    fprintf(
        stderr,
        "%s failed: %s\n",
        name,
        error != NULL ? error : cie_status_string(status)
    );
    return 1;
}

static bool callBinary(
    CieVM* vm,
    const char* name,
    CieValue left,
    CieValue right,
    CieValue* result
){
    CieValue args[] = {left, right};
    CieStatus status = cie_vm_call(vm, name, 2, args, result);
    if(status != CIE_STATUS_OK){
        reportCallFailure(vm, name, status);
        return false;
    }
    return true;
}

static bool expectNumber(CieVM* vm, double left, double right, double expected){
    CieValue result;
    if(!callBinary(
           vm,
           "mod",
           cie_value_number(left),
           cie_value_number(right),
           &result
       )){
        return false;
    }
    return result.type == CIE_VALUE_NUMBER && result.as.number == expected;
}

static bool expectEqual(CieVM* vm, CieValue left, CieValue right, bool expected){
    CieValue result;
    if(!callBinary(vm, "equal", left, right, &result)){
        return false;
    }
    return result.type == CIE_VALUE_BOOL && result.as.boolean == expected;
}

static bool expectZeroArgBool(CieVM* vm, const char* name, bool expected){
    CieValue result;
    CieStatus status = cie_vm_call(vm, name, 0, NULL, &result);
    if(status != CIE_STATUS_OK){
        reportCallFailure(vm, name, status);
        return false;
    }
    return result.type == CIE_VALUE_BOOL && result.as.boolean == expected;
}

int main(void){
    CieVM* vm = cie_vm_create();
    if(vm == NULL){
        fprintf(stderr, "Could not create Cieto VM.\n");
        return 1;
    }

    const char* source =
        "func mod(a, b) { return a % b; }\n"
        "func equal(a, b) { return a == b; }\n"
        "func stringsEqual() { return \"xabcx\".sub(1, 4) == \"abc\"; }\n"
        "func listsEqual() { return [1, [2, 3]] == [1, [2, 3]]; }\n"
        "func listsDiffer() { return [1, [2, 3]] == [1, [2, 4]]; }\n";

    CieStatus status = cie_vm_eval(vm, source, "runtime_numeric.cies");
    if(status != CIE_STATUS_OK){
        int failed = reportCallFailure(vm, "loading numeric tests", status);
        cie_vm_destroy(vm);
        return failed;
    }

    bool passed =
        expectNumber(vm, -5.0, 7.0, -5.0) &&
        expectNumber(vm, 5.0, -7.0, 5.0) &&
        expectNumber(vm, -8.0, 3.0, -2.0) &&
        expectNumber(vm, 5.5, 2.0, 1.5) &&
        expectNumber(vm, 5.0, INFINITY, 5.0) &&
        expectEqual(vm, cie_value_number(NAN), cie_value_number(NAN), false) &&
        expectEqual(vm, cie_value_number(0.0), cie_value_number(-0.0), true) &&
        expectEqual(vm, cie_value_bool(true), cie_value_bool(true), true) &&
        expectEqual(vm, cie_value_bool(true), cie_value_number(1.0), false) &&
        expectZeroArgBool(vm, "stringsEqual", true) &&
        expectZeroArgBool(vm, "listsEqual", true) &&
        expectZeroArgBool(vm, "listsDiffer", false);

    CieValue result;
    if(passed && callBinary(
           vm,
           "mod",
           cie_value_number(-0.0),
           cie_value_number(INFINITY),
           &result
       )){
        passed = result.type == CIE_VALUE_NUMBER &&
                 result.as.number == 0.0 && signbit(result.as.number);
    }else{
        passed = false;
    }

    if(passed && callBinary(
           vm,
           "mod",
           cie_value_number(INFINITY),
           cie_value_number(2.0),
           &result
       )){
        passed = result.type == CIE_VALUE_NUMBER && isnan(result.as.number);
    }else{
        passed = false;
    }

    CieValue zeroArgs[] = {cie_value_number(1.0), cie_value_number(0.0)};
    status = cie_vm_call(vm, "mod", 2, zeroArgs, &result);
    if(status != CIE_STATUS_RUNTIME_ERROR){
        fprintf(stderr, "Modulo by zero did not report a runtime error.\n");
        passed = false;
    }

    if(!expectNumber(vm, 5.0, 3.0, 2.0)){
        fprintf(stderr, "VM did not recover after modulo-by-zero.\n");
        passed = false;
    }

    cie_vm_destroy(vm);
    if(!passed){
        fprintf(stderr, "Numeric semantics regression test failed.\n");
        return 1;
    }

    printf("Numeric semantics tests passed.\n");
    return 0;
}
