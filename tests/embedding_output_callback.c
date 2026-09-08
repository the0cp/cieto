#include <stdio.h>
#include <string.h>

#include <cieto.h>

typedef struct Buffer{
    char data[1024];
    size_t length;
}Buffer;

static void bufferWrite(const char* text, size_t length, void* userData){
    Buffer* buffer = (Buffer*)userData;

    if(buffer->length + length >= sizeof(buffer->data)){
        length = sizeof(buffer->data) - buffer->length - 1;
    }

    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
}

int main(void){
    CieVM* vm = cie_vm_create();

    if(vm == NULL){
        fprintf(stderr, "Could not create Cieto VM.\n");
        return 1;
    }

    Buffer out = {0};
    Buffer err = {0};

    cie_vm_set_output(vm, bufferWrite, &out);
    cie_vm_set_error_output(vm, bufferWrite, &err);

    CieStatus status = cie_vm_eval(vm,
        "print \"hello\";\n"
        "print 42;\n"
        "print [1, true, null];\n",
        "<output_test>"
    );

    if(status != CIE_STATUS_OK){
        fprintf(stderr, "Unexpected eval failure: %s\n", cie_status_string(status));
        cie_vm_destroy(vm);
        return 1;
    }

    if(strcmp(out.data, "hello\n42\n[1, true, null]\n") != 0){
        fprintf(stderr, "Unexpected captured output: [%s]\n", out.data);
        cie_vm_destroy(vm);
        return 1;
    }

    status = cie_vm_eval(vm, "print 1 / 0;\n", "<error_test>");

    if(status != CIE_STATUS_RUNTIME_ERROR){
        fprintf(stderr, "Expected runtime error, got %s.\n", cie_status_string(status));
        cie_vm_destroy(vm);
        return 1;
    }

    if(strstr(err.data, "Division by zero") == NULL){
        fprintf(stderr, "Unexpected captured error: [%s]\n", err.data);
        cie_vm_destroy(vm);
        return 1;
    }

    err.length = 0;
    err.data[0] = '\0';
    status = cie_vm_eval(vm, "var value = ;\n", "<compile_error>");

    if(status != CIE_STATUS_COMPILE_ERROR){
        fprintf(stderr, "Expected compile error, got %s.\n", cie_status_string(status));
        cie_vm_destroy(vm);
        return 1;
    }

    const char* lastError = cie_vm_last_error(vm);
    if(strstr(err.data, "<compile_error>, line 1:13") == NULL ||
       lastError == NULL || strstr(lastError, "Expect expression") == NULL){
        fprintf(stderr, "Unexpected compile diagnostic: [%s]\n", err.data);
        cie_vm_destroy(vm);
        return 1;
    }

    printf("Captured output: %s", out.data);
    printf("Captured error: %s", err.data);

    cie_vm_destroy(vm);
    return 0;
}
