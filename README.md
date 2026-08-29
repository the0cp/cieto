# Cieto

A compact C-family scripting language and embeddable register-based virtual machine implemented entirely in C.

Cieto takes its name from Latin *ciētō*, a future imperative form of *cieō* (“to set in motion”).

## Features

- Register-based bytecode VM
- REPL
- Functions and closures
- Classes and methods
- Modules
- Lists, maps, strings, and slicing
- Small standard library
- Manual / automatic GC modes

See the included `manual.md` for a detailed language reference and usage examples: [https://github.com/the0cp/cieto/blob/master/manual.md](https://github.com/the0cp/cieto/blob/master/manual.md)

## What it looks like

```javascript
# A tiny Cieto demo:

func slug(s) {
    return s.trim().lower().replace(" ", "-");
}

func badge(s) {
    return "[" + s + "]";
}

func makeCounter(prefix) {
    var n = 0;

    return func(name) {
        n++;
        return "${prefix}-${n}: ${name}";
    };
}

var next = makeCounter("demo");
var topics = [" Register VM ", " Pipe Operator ", " Path Join "];

for (var topic : topics) {
    var name = topic |> slug |> badge;
    print next(name);
}

print "path: ${"examples" / "data" / "sample.txt"}";
print "slice: ${"register-vm"[0:8]}, reverse: ${"Cieto"[::-1]}";

$> echo hello from the host shell
print "shell exit code = ${_exit_code}";
```

## Examples

More examples are available in `examples/`.

Try more examples:

```sh
./build/debug/cieto examples/tour.cies
./build/debug/cieto examples/file_indexer.cies
./build/debug/cieto examples/modules/main.cies
# ...
```

## Building

Requirements: gcc and CMake. The code uses GCC-specific techniques such as computed goto / dispatch table, so GCC is required. On Windows, GCC can be installed through MinGW-w64, Chocolatey, or MSYS2.

Clone the repo:

```sh
git clone --recursive https://github.com/the0cp/cieto.git
```

Configure and build a debug version:

```sh
cmake --preset debug
cmake --build --preset debug
```

Configure and build a release version:

```sh
cmake --preset release
cmake --build --preset release
```

On Windows:

```sh
cmake --preset release-windows
cmake --build --preset release-windows
```

The executable is generated under the corresponding build directory, for example:

```text
build/debug/cieto
build/release/cieto
build/release-windows/cieto.exe
```

## Installing

Install Cieto to your local user prefix:

```sh
cmake --preset release
cmake --build --preset release
cmake --install build/release --prefix ~/.local
```

Make sure `~/.local/bin` is in your PATH:

```sh
export PATH="$HOME/.local/bin:$PATH"
```

Then run Cieto from anywhere.

To install system-wide:

```sh
sudo cmake --install build/release
```

## Embedding Cieto in C

Cieto can also be used as an embedded scripting VM inside a C program. The host program owns a `CieVM`, loads Cieto source code, registers native C functions, calls Cieto functions, and can capture script output and runtime errors.

A minimal embedded program looks like this:

```c
#include <stdio.h>
#include <cieto.h>

int main(void){
    CieVM* vm = cie_vm_create();

    if(vm == NULL){
        return 1;
    }

    CieStatus status = cie_vm_eval(vm,
        "func add(a, b){ return a + b; }\n",
        "<embedded>"
    );

    if(status != CIE_STATUS_OK){
        fprintf(stderr, "%s\n", cie_vm_last_error(vm));
        cie_vm_destroy(vm);
        return 1;
    }

    CieValue args[] = {
        cie_value_number(20),
        cie_value_number(22)
    };

    CieValue result;
    status = cie_vm_call(vm, "add", 2, args, &result);

    if(status == CIE_STATUS_OK && result.type == CIE_VALUE_NUMBER){
        printf("%.14g\n", result.as.number);
    }

    cie_vm_destroy(vm);
    return status == CIE_STATUS_OK ? 0 : 1;
}
```

The third argument of `cie_vm_eval()` is a diagnostic source name. It does not need to be a real file path; names like `"<embedded>"` or `"<plugin>"` are useful for error messages.

The embedding API currently supports these core operations:

- `cie_vm_create()` / `cie_vm_destroy()` for VM lifetime management
- `cie_vm_eval()` for loading Cieto source code
- `cie_vm_register_native()` for exposing C functions to Cieto
- `cie_vm_call()` for calling global Cieto functions from C
- `cie_vm_set_output()` and `cie_vm_set_error_output()` for capturing `print` output and runtime error output
- `cie_vm_last_error()` for reading the latest compile or runtime error

More complete examples are in `examples/embedding/`.

### Linking against an installed libcieto

After installation, an external C program can include `cieto.h` and link against `libcieto.a`:

```sh
gcc main.c -I /path/to/cieto/include -L /path/to/cieto/lib -lcieto -lm -o embed_app
```

On Windows with MinGW, the command is the same except paths usually point to the local install prefix, for example:

```powershell
gcc .\main.c `
    -I .\install-debug\include `
    -L .\install-debug\lib `
    -lcieto `
    -lm `
    -o .\embed_app.exe
```

## Testing

Run the test suite with *CTest*:

```sh
ctest --preset debug --output-on-failure
```

or for release:

```sh
ctest --preset release --output-on-failure
```

CTest runs the script suite normally and compares the same scripts against `--no-opt`, so compiler optimizations must preserve stdout, stderr, and exit status.

## Usage

Run the interactive REPL:

```sh
cieto
```

Run a script:

```sh
cieto path/to/script.cies
```

Run a script without compiler optimizations:

```sh
cieto --no-opt path/to/script.cies
```

Dump bytecode:

```sh
cieto --dump path/to/script.cies
cieto --no-opt --dump path/to/script.cies
```

Cieto scripts can also be executed directly with a Unix shebang:

```sh
#!/usr/bin/env cieto

print("hello from Shebang");
```

Make it executable and run it:

```sh
chmod +x hello.cies
./hello.cies
```

Check `manual.md` for language syntax, built-in functions, and examples.

## License

This project is distributed under the GNU GPL v3. See `gpl-3.0.txt` for details.
