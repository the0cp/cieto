#include <stdio.h>

#include <cieto.h>

int main(void){
    CieVM* vm = cie_vm_create();

    if(vm == NULL){
        fprintf(stderr, "Could not create Cieto VM.\n");
        return 1;
    }

    CieStatus status = cie_vm_eval(vm,
        "var name = \"Cieto\";\n"
        "print(\"Hello from ${name}!\");\n",
        "<basic>"
    );

    if(status != CIE_STATUS_OK){
        const char* error = cie_vm_last_error(vm);
        fprintf(stderr, "%s\n", error != NULL ? error : cie_status_string(status));
        cie_vm_destroy(vm);
        return 1;
    }

    cie_vm_destroy(vm);
    return 0;
}
