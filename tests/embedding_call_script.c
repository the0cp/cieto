#include <stdio.h>
#include <string.h>

#include <cieto.h>

static void hostAdd(CieCall* call, void* userData) {
    (void)userData;

    if(cie_call_arg_count(call) != 2){
        cie_call_error(call, "hostAdd() expects exactly two arguments.");
        return;
    }

    double left;
    double right;

    if(!cie_call_get_number(call, 0, &left) ||
       !cie_call_get_number(call, 1, &right)){
        cie_call_error(call, "hostAdd() expects two numbers.");
        return;
    }

    cie_call_return_number(call, left + right);
}

static int reportFailure(CieVM* vm, const char* operation,
                         CieStatus status) {
    const char* error = cie_vm_last_error(vm);

    fprintf(stderr, "%s failed: %s\n", operation,
            error != NULL ? error : cie_status_string(status));

    return 1;
}

int main(void) {
    CieVM* vm = cie_vm_create();

    if(vm == NULL){
        fprintf(stderr, "Could not create Cieto VM.\n");
        return 1;
    }

    CieStatus status =
        cie_vm_register_native(vm, "hostAdd", hostAdd, NULL);

    if(status != CIE_STATUS_OK){
        int exitCode = reportFailure(vm, "Registering hostAdd", status);
        cie_vm_destroy(vm);
        return exitCode;
    }

    const char* source =
        "func add(a, b) {\n"
        "    return a + b;\n"
        "}\n"
        "\n"
        "func calculate(a, b) {\n"
        "    return hostAdd(a, b) * 2;\n"
        "}\n"
        "\n"
        "func failCall() {\n"
        "    return hostAdd(\"invalid\", 1);\n"
        "}\n"
        "\n"
        "var notCallable = 42;\n";

    status = cie_vm_eval(vm, source, "embedding_call_script.cies");

    if(status != CIE_STATUS_OK){
        int exitCode = reportFailure(vm, "Loading Cieto functions", status);
        cie_vm_destroy(vm);
        return exitCode;
    }

    CieValue addArgs[] = {
        cie_value_number(20),
        cie_value_number(22)
    };

    CieValue result;
    status = cie_vm_call(vm, "add", 2, addArgs, &result);

    if(status != CIE_STATUS_OK){
        int exitCode = reportFailure(vm, "Calling add", status);
        cie_vm_destroy(vm);
        return exitCode;
    }

    if(result.type != CIE_VALUE_NUMBER || result.as.number != 42.0){
        fprintf(stderr, "Expected add() to return 42.\n");
        cie_vm_destroy(vm);
        return 1;
    }

    printf("Cieto add result: %.14g\n", result.as.number);

    CieValue calculateArgs[] = {
        cie_value_number(10),
        cie_value_number(11)
    };

    status = cie_vm_call(vm, "calculate", 2, calculateArgs, &result);

    if(status != CIE_STATUS_OK){
        int exitCode = reportFailure(vm, "Calling calculate", status);
        cie_vm_destroy(vm);
        return exitCode;
    }

    if(result.type != CIE_VALUE_NUMBER || result.as.number != 42.0){
        fprintf(stderr, "Expected calculate() to return 42.\n");
        cie_vm_destroy(vm);
        return 1;
    }

    printf("C -> Cieto -> C result: %.14g\n", result.as.number);

    status = cie_vm_call(vm, "missingFunction", 0, NULL, &result);

    if(status != CIE_STATUS_RUNTIME_ERROR){
        fprintf(stderr, "Expected missing function call to fail.\n");
        cie_vm_destroy(vm);
        return 1;
    }

    const char* error = cie_vm_last_error(vm);

    if(error == NULL || strstr(error, "not defined") == NULL){
        fprintf(stderr, "Unexpected missing-function error: %s\n",
                error != NULL ? error : "<none>");
        cie_vm_destroy(vm);
        return 1;
    }

    printf("Captured missing function error: %s\n", error);

    status = cie_vm_call(vm, "notCallable", 0, NULL, &result);

    if(status != CIE_STATUS_RUNTIME_ERROR){
        fprintf(stderr, "Expected non-callable global to fail.\n");
        cie_vm_destroy(vm);
        return 1;
    }

    error = cie_vm_last_error(vm);

    if(error == NULL || strstr(error, "Can only call") == NULL){
        fprintf(stderr, "Unexpected non-callable error: %s\n",
                error != NULL ? error : "<none>");
        cie_vm_destroy(vm);
        return 1;
    }

    printf("Captured non-callable error: %s\n", error);

    status = cie_vm_call(vm, "failCall", 0, NULL, &result);

    if(status != CIE_STATUS_RUNTIME_ERROR){
        fprintf(stderr, "Expected failCall() to produce a runtime error.\n");
        cie_vm_destroy(vm);
        return 1;
    }

    error = cie_vm_last_error(vm);

    if(error == NULL || strstr(error, "expects two numbers") == NULL){
        fprintf(stderr, "Unexpected nested callback error: %s\n",
                error != NULL ? error : "<none>");
        cie_vm_destroy(vm);
        return 1;
    }

    printf("Captured nested callback error: %s\n", error);

    /*
     * Verify that the VM remains usable after a failed host-triggered call.
    */
    status = cie_vm_call(vm, "add", 2, addArgs, &result);

    if(status != CIE_STATUS_OK ||
       result.type != CIE_VALUE_NUMBER ||
       result.as.number != 42.0){
        int exitCode = reportFailure(vm, "Calling add after recovery", status);
        cie_vm_destroy(vm);
        return exitCode;
    }

    printf("VM recovered after failed call: %.14g\n", result.as.number);

    const struct {
        const char* source;
        const char* expectedError;
    } iteratorErrors[] = {
        {"iter();\n", "iter expects exactly one argument"},
        {"var advanceIt = iter([1]); advanceIt.advance(1);\n", "iterator.advance expects no arguments"},
        {"var currentIt = iter([1]); currentIt.current();\n", "iterator.current is unavailable"},
    };

    for(size_t i = 0; i < sizeof(iteratorErrors) / sizeof(iteratorErrors[0]); i++){
        status = cie_vm_eval(vm, iteratorErrors[i].source, "iterator_error.cies");
        error = cie_vm_last_error(vm);

        if(status != CIE_STATUS_RUNTIME_ERROR || error == NULL ||
           strstr(error, iteratorErrors[i].expectedError) == NULL){
            fprintf(stderr, "Unexpected iterator API result: %s\n",
                    error != NULL ? error : cie_status_string(status));
            cie_vm_destroy(vm);
            return 1;
        }
    }

    cie_vm_destroy(vm);
    return 0;
}
