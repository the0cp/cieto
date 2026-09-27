# Embedding examples

These examples show how to use Cieto as an embedded scripting VM from C.

- `basic.c`: create a VM and evaluate Cieto source code.
- `native_function.c`: expose a C function to Cieto.
- `call_script.c`: call a Cieto function from C and let the Cieto function call back into C.

## `basic.c`

Creates a VM, evaluates a Cieto source string, and destroys the VM.

```sh
cmake --build --preset debug --target cieto_embed_basic
./build/debug/cieto_embed_basic
```

## `native_function.c`

Registers a C function and calls it from Cieto.

```c
cie_vm_register_native(vm, "hostAdd", hostAdd, NULL);
```

Cieto can then call it as a normal function:

```javascript
print(hostAdd(20, 22));
```

## `call_script.c`

Loads a Cieto function and calls it from C. The Cieto function calls back into a native C function.

This demonstrates the full embedding loop:

```text
C -> Cieto -> C -> Cieto -> C
```

## `external-cmake/`

A minimal standalone CMake project that links against an installed `libcieto.a`.

After installing Cieto into a local prefix, configure it with:

```sh
cmake -S examples/embedding/external-cmake -B build/external-cieto -DCIETO_ROOT=/path/to/cieto/install
cmake --build build/external-cieto
```

On Windows PowerShell, for a local debug install:

```powershell
cmake -S .\examples\embedding\external-cmake -B .\build\external-cieto -DCIETO_ROOT="$PWD\install-debug"
cmake --build .\build\external-cieto
.\build\external-cieto\embed_app.exe
```

## Source names

The third argument of `cie_vm_eval()` is a diagnostic name:

```c
cie_vm_eval(vm, source, "<script>");
```

It does not need to be a real file. It is used in error messages.

