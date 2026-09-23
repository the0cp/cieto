#include <stdio.h>
#include <string.h>

#include <cieto.h>

static int expectRuntimeError(const char* name, const char* source,
                              const char* expectedError){
    CieVM* vm = cie_vm_create();
    if(vm == NULL){
        fprintf(stderr, "%s: could not create Cieto VM.\n", name);
        return 1;
    }

    CieStatus status = cie_vm_eval(vm, source, name);
    const char* error = cie_vm_last_error(vm);
    int failed = status != CIE_STATUS_RUNTIME_ERROR || error == NULL ||
                 strstr(error, expectedError) == NULL;

    if(failed){
        fprintf(stderr, "%s: expected runtime error containing '%s', got: %s\n",
                name, expectedError,
                error != NULL ? error : cie_status_string(status));
    }

    cie_vm_destroy(vm);
    return failed;
}

int main(void){
    const char* testPath = "cieto_runtime_fs_test.tmp";
    const char* missingPath = "cieto_runtime_fs_missing_7b6126f0.tmp";
    int failed = 0;

    (void)remove(testPath);
    (void)remove(missingPath);

    failed += expectRuntimeError(
        "fs_open_missing.cies",
        "import \"fs\";\n"
        "fs.open(\"cieto_runtime_fs_missing_7b6126f0.tmp\", \"r\");\n",
        "Could not open file");

    failed += expectRuntimeError(
        "fs_closed_foreach.cies",
        "import \"fs\";\n"
        "var file = fs.open(\"cieto_runtime_fs_test.tmp\", \"w\");\n"
        "file.close();\n"
        "for (var line : file) {}\n",
        "Cannot iterate a closed file");

    failed += expectRuntimeError(
        "fs_foreach_read_error.cies",
        "import \"fs\";\n"
        "var file = fs.open(\"cieto_runtime_fs_test.tmp\", \"w\");\n"
        "for (var line : file) {}\n",
        "Could not read from file while iterating");

    failed += expectRuntimeError(
        "fs_iterator_read_error.cies",
        "import \"fs\";\n"
        "var file = fs.open(\"cieto_runtime_fs_test.tmp\", \"w\");\n"
        "var iterator = iter(file);\n"
        "iterator.advance();\n",
        "Could not read from file while iterating");

    failed += expectRuntimeError(
        "fs_file_write_error.cies",
        "import \"fs\";\n"
        "fs.write(\"cieto_runtime_fs_test.tmp\", \"seed\");\n"
        "var file = fs.open(\"cieto_runtime_fs_test.tmp\", \"r\");\n"
        "file.write(\"not writable\");\n",
        "Could not write all content to file");

    failed += expectRuntimeError(
        "fs_file_read_error.cies",
        "import \"fs\";\n"
        "var file = fs.open(\"cieto_runtime_fs_test.tmp\", \"w\");\n"
        "file.read();\n",
        "Could not read file");

    failed += expectRuntimeError(
        "fs_file_read_line_error.cies",
        "import \"fs\";\n"
        "var file = fs.open(\"cieto_runtime_fs_test.tmp\", \"w\");\n"
        "file.readLine();\n",
        "Could not read line from file");

    (void)remove(testPath);
    (void)remove(missingPath);

    return failed == 0 ? 0 : 1;
}
