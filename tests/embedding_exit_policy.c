#include <stdio.h>
#include <string.h>

#include <cieto.h>

int main(void){
    CieVM* vm = cie_vm_create();

    if(vm == NULL){
        fprintf(stderr, "Could not create Cieto VM.\n");
        return 1;
    }

    const char* source =
        "import \"os\";\n"
        "os.exit(7);\n"
        "print(\"This line must not execute.\");\n";

    CieStatus status = cie_vm_eval(
        vm,
        source,
        "embedding_exit_policy.cies"
    );

    if(status != CIE_STATUS_RUNTIME_ERROR){
        fprintf(stderr, "Expected runtime error, got: %s\n", cie_status_string(status));

        cie_vm_destroy(vm);
        return 1;
    }

    const char* error = cie_vm_last_error(vm);

    if(error == NULL){
        fprintf(stderr, "Expected an error message.\n");
        cie_vm_destroy(vm);
        return 1;
    }

    if(strstr(error, "disabled") == NULL){
        fprintf(stderr, "Unexpected error message: %s\n", error);

        cie_vm_destroy(vm);
        return 1;
    }

    printf("Host survived os.exit().\n");
    printf("Captured error: %s\n", error);

    cie_vm_destroy(vm);
    return 0;
}
