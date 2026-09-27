#include <stdio.h>

#include <cieto.h>

static void hostAdd(CieCall* call, void* userData){
    (void)userData;

    double left;
    double right;

    if(!cie_call_get_number(call, 0, &left) || !cie_call_get_number(call, 1, &right)){
        cie_call_error(call, "hostAdd() expects two numbers.");
        return;
    }

    cie_call_return_number(call, left + right);
}

int main(void){
    CieVM* vm = cie_vm_create();

    if(vm == NULL){
        fprintf(stderr, "Could not create Cieto VM.\n");
        return 1;
    }

    cie_vm_register_native(vm, "hostAdd", hostAdd, NULL);

    CieStatus status = cie_vm_eval(vm,
        "var result = hostAdd(20, 22);\n"
        "print(result);\n",
        "<native_function>"
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
