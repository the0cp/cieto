# Manual

Cieto source files generally use the `.cies` extension. The language syntax is inspired by the C/JavaScript family.

## Statements

Statements must be terminated with a semicolon `;`.

## Comments

Cieto supports two types of comments:

- **Single-line comments**: Start with `#` and continue to the end of the line.

- **Block comments**: Start with `#{` and end with `}#`. A key feature is that block comments can be nested.

Examples:

```javascript
# This is a single-line comment

#{ 
   This is a block comment.
   #{ Nested comments are supported }#
}#
```

## Data Types

Cieto is dynamically typed. It supports the following fundamental types:

- **null**: Represents the absence of a value. Keyword: `null`.

- **boolean**: Logical values. Keywords: `true`, `false`.

- **number**: Double-precision floating-point numbers (C `double`).

- **string**: Immutable sequence of characters.

- **list**: Dynamic-typed array/list.

- **object**: Complex types including functions , modules, classes, instances, files, etc..

## Literals & Variables

### Strings and Interpolation

Strings are enclosed in double quotes `"`.

Cieto supports interpolation, allowing expressions to be embedded directly within string literals. Supports standard escapes like `\n`, `\t`, `\r`, `\"`, `\\`, and `\$`.

Example:

```javascript
var name = "Cieto";
print "Hello, ${name}!"; 
# Output: Hello, Cieto!
```

### Lists

Lists are defined using square brackets `[]`.

- **Standard Definition**: `var list = [1, 2, "three"];`.

- **Bulk Fill Initialization**: `[value; count]` creates a list of `count` elements, all initialized to `value`.

Example:

```javascript
var zeros = [0; 5]; # Creates [0, 0, 0, 0, 0]
```

### Maps

Maps are key-value pairs enclosed in curly braces `{}`.

- **Syntax**: `{ key1: value1, key2: value2 }`.
- **Access**: Values are accessed using square brackets `[]` with the key.

> Map keys support string, integer numbers, bool, and null. Using a float with a fractional part (e.g., `1.5`) as a key will result in a runtime error. However, `1.0` is treated as integer `1`.

Example:

```javascript
var dict = { 
    "name": "Cieto",
    "version": 1, 
    true: "Verified" 
};

print dict["name"]; # Output: Cieto
dict["new_key"] = 100;
```

### Variable Declaration

Variables are declared using the `var` keyword.

- Variables are local to their scope unless defined at the top level.

- If declared without an initial value, they default to `null`.

Example:

```javascript
var x = 10;
var y;      
# y is null
```

## Operators

Cieto supports standard operators with precedence rules defined in the compiler.

### Arithmetic

- `+` (Addition), `-` (Subtraction), `*` (Multiplication), `/` (Division), `%` (Modulo).

- Increment & Decrement: `++`, `--`.

Prefix (`++i`, `--i`): Increments/Decrements the value and returns the new value. Only supported for variables.

Postfix (`i++`, `i--`): Increments/Decrements the value and returns the original value. Supported for variables, object properties, and list indices.

Example:

```javascript
var a = 15;
print ++a;      # Output: 16 (a is 16)
print a++;      # Output: 16 (a becomes 17)

class Box { 
  Val = 0;
}

var obj = Box();
obj.Val++;      # Supported (Postfix)

var list = [10, 20];
list[0]++;      # Supported (Postfix), list[0] becomes 11
```

- **String Concatenation**: The `+` operator is overloaded to concatenate strings if operands are strings.

- **Unary**: `-` (Negation).

### Assignment & Increment

Cieto provides shorthand operators for modifying variables and properties.

- Compound Assignment: `+=`, `-=`.

  - `a += b` is equivalent to `a = a + b`.

  - `a -= b` is equivalent to `a = a - b`.

### Implicit String Concatenation

If either operand of the `+` operator is a String, the other operand is automatically converted to a String, and the result is their concatenation.

Example:

```javascript
var age = 20;
print "Age: " + age;  # Output: "Age: 20"
print 100 + "%";      # Output: "100%"
```

### Path Concatenation

When the division operator `/` is used with two strings, it functions as a platform-independent path joiner.

Example:

```javascript
var path = "users" / "docs" / "report.txt";
# Linux/Mac: "users/docs/report.txt"
# Windows:   "users\docs\report.txt"
```

### Comparison

- `==`, `!=`, `<`, `>`, `<=`, `>=`.

- Comparisons return `true` or `false`.

> Lists are compared by value, not by reference. Two separate list objects containing the same elements in the same order are considered equal. `[1, 2] == [1, 2]; # true`

### Ternary Operator

Cieto supports the conditional (ternary) operator, which is the only operator that takes three operands. It is frequently used as a shortcut for the `if` statement.

- *Syntax*: `condition ? expression1 : expression2`
- *Behavior*: If `condition` is truthy, `expression1` is evaluated and returned; otherwise, `expression2` is evaluated and returned.

Example:

```javascript
var age = 20;
var status = age >= 18 ? "Adult" : "Minor";
print status; # Output: Adult
```

### Logic

- `and`: Logical AND (short-circuit evaluation).

- `or`: Logical OR (short-circuit evaluation).

- `!`: Logical NOT.

### Pipe Operator

The pipe operator `|>` allows for chaining function calls in a readable, left-to-right manner. It takes the result of the expression on the left and passes it as the *first argument* to the function on the right.

- **Syntax**: `x |> f` is equivalent to `f(x)`.
- **Chaining**: `x |> f |> g` is equivalent to `g(f(x))`.

Example:

```javascript
func addOne(n) { 
    return n + 1; 
}

func double(n) { 
    return n * 2; 
}

print double(addOne(5)); # Output: 12

5 |> addOne |> double |> println; # Output: 12
"hello" |> func(s) { return s + " world"; } |> println; # Output: hello world
```

### Indexing & Slicing

Cieto supports powerful indexing and slicing operations for both **Lists** and **Strings**.

#### Indexing

Access a single element using square brackets `[]`.

- **Syntax**: `list[index]`.

- **Negative Indexing**: Negative integers index from the end of the list (e.g., `list[-1]` accesses the last item).

Example:

```javascript
var list = [10, 20, 30]; 
print list[0]; # 10 
print list[-1]; # 30
```

#### Slicing

Extract a subsequence using the slice operator `:`. This creates a new string or list.

- **Syntax**: `sequence[start : end : step]`

- **Parameters**:
  - `start` (optional): The starting index (inclusive). Defaults to `0` (or end if step is negative).
  - `end` (optional): The ending index (exclusive). Defaults to length (or beginning if step is negative).
  - `step` (optional): The increment size. Defaults to `1`.

- **Behavior**:
  - **Clamping**: Out-of-bounds indices are automatically clamped to valid ranges. No errors are thrown for safe slicing.
  - **Negative Step**: If `step` is negative, the sequence is traversed in reverse.

Examples:

```javascript
var s = "Hello World";

# Basic Slicing
print s[0:5]; # "Hello" 
print s[6:]; # "World" (6 to end) 
print s[:5]; # "Hello" (Beginning to 5)

# Negative Indices
print s[-5:]; # "World"

# Step & Reversal
print s[::2]; # "HloWrd" (Every 2nd char) 
print s[::-1]; # "dlroW olleH" (Reverse string)

# Lists work the same way
var list = [1, 2, 3, 4, 5]; 
print list[1:4]; # [2, 3, 4]
```

## Control Flow & Statements

### Truthy and Falsy Values

In conditional statements (like `if` or `while`), values are evaluated as follows:

- **Falsy**: `false`, `null`, and the number `0`.

- **Truthy**: All other values (including empty strings `""` and empty lists `[]`).

### if-else

The `if` statement executes a block of code if a condition is `true`. An optional `else` block can be provided.

- Example:

```javascript
if(condition){
    # body
}else{
    # else body
}
```

- **Parentheses**: The condition must be enclosed in parentheses `()`.

- **Scope**: Blocks `{}` create a new scope.

### Loops

Cieto supports `while` and C-style `for` loops. Both support `break` and `continue`.

#### while

Repeats a block of code as long as the condition is true.

```javascript
while(condition){
    # body
}
```

#### for

Standard C-style for loop with initialization, condition, and increment clauses.

```javascript
for(var i = 0; i < 10; i = i + 1){
    print i;
}
```

All three clauses are optional (e.g., `for (;;) { ... }` creates an infinite loop).

#### foreach

Cieto supports a simplified syntax for iterating over any iterable object (List, Map, File, etc.) using the `:` operator. This is syntactic sugar for the underlying `iter()` and `next()` protocol.

- **Syntax**: `for (var item : iterable) { ... }`

Example:

```javascript
# Iterating over a List
var nums = [10, 20, 30];
for (var n : nums) {
    print n;
}

#{
   output:
   10
   20
   30
}#

# Iterating over a Map (Keys)
var dict = { "a": 1, "b": 2 }; 
for (var key : dict) { 
    print key; # Prints "a" or "b" 
}

# Iterating over a File (Lazy Line-by-Line)
var f = fs.open("log.txt", "r");
for (var line : f) { 
    print line; 
} 
f.close();
```

#### break & continue

- `break`: Exits the current nearest loop immediately.

- `continue`: Skips the rest of the loop body and jumps to the next iteration (or the increment clause in a `for` loop).

### Switch Statement

Cieto provides a `switch` statement for matching a value against multiple possibilities.

Example:

```javascript
switch(value){
    case 1, 2 => {
        print "One or Two";
    }
    case 3 => print "Three";
    default => print "Other";
}
```

- **Arrow**: Uses `=>` to separate the case value from the body.

- **Multiple Matches**: A single `case` can match multiple values separated by commas (e.g., `case 1, 2`).

- **No Fallthrough**: Unlike C, Cieto switches **do not** fall through automatically. You do not need to write `break`.

- **Default**: An optional `default` case handles values not matched by any case.

### Defer Statement

The `defer` statement defers the execution of a statement (or a block of code) until the surrounding function returns.

- **Syntax**: `defer statement;`
- **Execution Order**: Deferred statements are executed in **Last In, First Out (LIFO)** order. The last deferred statement is executed first when the function exits.
- **Scope**: `defer` is function-scoped. It executes when the enclosing function returns, regardless of where `return` is called.

Example:

```javascript
func processFile() {
    var f = fs.open("log.txt", "w");
    if (!f) return;
    
    # Ensure file is closed when function exits, 
    # even if an error occurs later.
    defer f.close();
    
    defer print "Function finishing...";

    f.write("Log entry");
}

func test() {
    print("Function enter");
    
    defer {
        print("Defer 1 (Last In, First Out)");
    }

    defer print("Defer 2");

    print("Function exit");
    return "Result";
}

var res = test();

```

### Command Statement

Cieto features a simple syntax for executing system shell commands directly.

- **Syntax**: `$> command`

- **Behavior**:
  
  1. Pauses Cieto execution.
  
  2. Passes the rest of the line (after `$>` ) to the underlying system shell.
  
  3. Prints the command's output directly to the standard output (stdout).
  
  4. $> is a statement, not an expression. It executes the rest of the line through the system shell. The normalized exit code is stored in the global variable _exit_code.
  
  5. $> does not require a semicolon. It ends at the end of the line.

Exit Status (`_exit_code`): Unlike standard function calls, the `$>` command does not return a value directly to the expression. Instead, the exit status of the executed command is stored in a special global variable named `_exit_code`.

`0`: Typically indicates success.

Non-zero: Indicates an error or a specific status code returned by the command.

Example:

```javascript
print "Listing files:";
$> ls -la
print "Done.";
```

## Iterators

Cieto provides a built-in iterator protocol to traverse collections efficiently. This mechanism underpins the `foreach` loop but can also be used manually.

Cieto exposes two global functions to work with iterators:

- `iter(iterable)`
  
  - *Description*: Creates and returns an **Iterator** object for the given receiver.
  
  - *Supported Types*: List, Map, File.
  
  - *Returns*: Iterator Object.

- `next(iterator)`
  
  - *Description*: Advances the iterator and returns the next value.
  
  - *Returns*: The next value, or `null` if the iteration has finished.

## Functions

Functions in Cieto are first-class citizens (internally). Current syntax supports function declarations primarily as statements. They support closures, capturing variables from their enclosing scope.

### Function Declaration

Use the `func` keyword to define a function.

```javascript
func add(a, b){
    return a + b;
}
```

The `return` keyword exits the function with a value. If no value is returned, it implicitly returns `null`.

### Closures

Functions can access variables defined in their outer scopes (by upvalues).

```javascript
func makeCounter(){
    var count = 0;
    func increment(){
        count = count + 1;
        return count;
    }
    return increment;
}

var counter = makeCounter();
print counter(); # 1
print counter(); # 2
```

### Anonymous Functions

Functions can be created without a name and used as expressions. This is particularly useful for assigning functions to variables, passing them as arguments, or using them in pipe operations.

Example:

```javascript
var square = func(x) { return x * x; };
print square(5); # Output: 25
```

## Class & Objects

Cieto implements a unique flavor of Object-Oriented Programming (OOP) that separates **data definition** (classes) from **behavior definition** (methods), similar to Go or Rust's approach, but with explicit class definitions for fields.

### Class Definition (Fields)

The `class` block is strictly for defining data structures (fields) and their default values.

```javascript
class Point{
    x = 0;
    y = 0;
    name; # Defaults to null
}
```

Call the class name like a function to create a new instance.

```javascript
var p = Point();
p.x = 10;
```

### Method Definition

Methods are defined **outside** the class body using the `method` keyword. This syntax explicitly binds a function to a receiver type.

**Syntax**: `method (receiver_name ClassName) MethodName(args...) { ... }`

```javascript
method (p Point) move(dx, dy){
    p.x = p.x + dx;
    p.y = p.y + dy;
}

var p = Point();
p.move(5, 5);
```

Inside a method, you can use the `this` keyword to refer to the instance (receiver), or use the name you defined in the method signature (`p` in the example above). Both refer to the same object (Local slot 0).

### Constructors

Cieto uses a special method named `init` as the constructor to initialize new instances.

- **Automatic Invocation**: When you instantiate a class (e.g., `var p = Point(10, 20);`), Cieto automatically calls the `init` method matching that class with the provided arguments.
- **Implicit Return**: The `init` method is special; it implicitly returns the new instance (`this`). You do not need to write `return`, and returning a specific value from `init` is a syntax error.

Example:

```javascript
class Point {
    x = 0;
    y = 0;
}

# The constructor method
method (p Point) init(x, y) {
    p.x = x;
    p.y = y;
    # 'this' (p) is returned automatically
}

var p = Point(10, 20); # Calls init implicitly
```

### Visibility

Cieto enforces access control based on **Capitalization**.

- **Public**: Field names starting with an Uppercase letter (e.g., `Name`, `ID`) are accessible from anywhere.

- **Private**: Field names starting with a lowercase letter (e.g., `age`, `hidden`) are only accessible within methods of that class.

Example:

```javascript
class User{
    Name;  # Public
    age;   # Private
}

method (u User) getAge(){
    return u.age; # Accessed inside a method of User
}

var u = User();
u.Name = "Alice";
# u.age = 25;     # Runtime Error: Cannot access private field 'age'
```

## Modules

Cieto supports two kinds of modules:

1. Script modules: user-defined `.cies` files.
2. Native standard modules: built-in modules implemented by the runtime, such as `fs`, `os`, `time`, `path`, `glob`, and `gc`.

### Importing

Use the `import` keyword to load another `.cies` file.

- **Syntax**: `import "path/to/mod.cies";`

- **Behavior**:
  
  1. Cieto executes the referenced file.
  
  2. All global variables defined in that file become properties of the module object.
  
  3. A variable with the same name as the file (without extension) is automatically created in the current scope to reference the module.

Example:

- FIle `math.cies`:

```javascript
func add(a, b) { return a + b; }
var PI = 3.14;
```

- File `main.cies`:

```javascript
import "math.cies"; # Creates variable 'math'

print math.PI;
print math.add(10, 20);
```

### Automatic Module Naming

When importing a module, Cieto automatically creates a variable derived from the filename (without extension and directory path) to reference the module. Custom aliasing (e.g., `as`) is not currently supported.

Example:

```javascript
import "./utils/logger.cies";
# Automatically creates a variable named 'logger'
logger.log("Ready");
```

## Standard Libs

Core value types such as strings and lists do not require imports; their operations are exposed as built-in object methods.

### fs - File System Modules

The `fs` module provides functionality for interacting with the file system.

#### Import

```javascript
import "fs";
```

#### Functions

- `fs.read(path)`
  
  - *Description*: Reads the entire content of the file at `path`.
  
  - *Arguments*: `path` (String).
  
  - *Returns*: String (content).

- `fs.write(path, content)`
  
  - *Description*: Writes `content` to the file at `path`. **Overwrites** the file if it exists.
  
  - *Arguments*: `path` (String), `content` (String).
  
  - *Returns*: `true` on success, `null` on failure.

- `fs.append(path, content)`
  
  - *Description*: Appends `content` to the end of the file at `path`.
  
  - *Arguments*: `path` (String), `content` (String).
  
  - *Returns*: `true` on success.

- `fs.exists(path)`
  
  - *Description*: Checks if a file exists at the given path.
  
  - *Returns*: `true` if exists, `false` otherwise.

- `fs.remove(path)`
  
  - *Description*: Deletes the file at `path`.
  
  - *Returns*: `true` on success.

- `fs.list(arg)`
  
  - *Description*: Lists all files and directories in the specified directory. If `arg` is a string, it treats it as a directory path and lists all files/directories in it (excluding `.` and `..`). If `arg` is a `glob.Glob` object, it performs a search based on the object's configuration.
  
  - *Returns*: A List of Strings (filenames).

- `fs.rlines(path)`
  
  - *Description*: Reads the file line by line.
  
  - *Returns*: A List of Strings, where each item is a line from the file.

- `fs.mkdir(path)`
  
  - *Description*: Creates a new directory.
  
  - *Returns*: `true` on success.

- `fs.isDir(path)`
  
  - *Description*: Checks if the path points to a directory.
  
  - *Returns*: `true` if it is a directory.

- `fs.open(path, [mode])`
  
  - *Description*: Opens a file and returns a iterable **File Object** for advanced operations.
  
  - *Arguments*:

    - `path` (String).

    - `mode` (String, optional): "r" (read), "w" (write), etc. Defaults to "r".
  
  - *Returns*: A File Object.

### os - Operating System Module

The `os` module provides tools to interact with the underlying operating system.

#### Import

```javascript
import "os";
```

#### Variables

- `os.argv`
  
  - *Description*: A List of Strings containing the command-line arguments passed to the Cieto script. `os.argv[0]` is the script name.

#### Functions

- `os.run(command)`
  
  - *Description*: Executes a shell command using `system()`.
  
  - *Arguments*: `command` (String).
  
  - *Returns*: The exit status code (number).

- `os.exec(command)`
  
  - *Description*: Executes a shell command and captures its output (stdout).
  
  - *Arguments*: `command` (String).
  
  - *Returns*: String (the output of the command).

- `os.getenv(name)`
  
  - *Description*: Gets the value of an environment variable.
  
  - *Returns*: String (value) or `null` if not found.

- `os.setenv(key, value)`
  
  - *Description*: Sets or creates an environment variable.
  
  - *Arguments*:
    - `key` (String).
    - `value` (String).
  
  - *Returns*: `true` on success, `false` on failure.

- `os.exit(code)`
  
  - *Description*: Terminates the program immediately with the given exit code.
  
  - *Arguments*: `code` (Number).

### time - Time Module

#### Import

```javascript
import "time";
```

#### Functions

- `time.now()`
  
  - *Returns*: The current high-resolution wall-clock time in seconds (Double).

- `time.steady()`
  
  - *Returns*: A monotonic clock time in seconds, suitable for measuring intervals.

- `time.clock()`
  
  - *Returns*: System time in seconds (Standard C `time(NULL)`).

- `time.sleep(seconds)`
  
  - *Description*: Pauses execution for the specified number of seconds.
  
  - *Arguments*: `seconds` (Number).

- `time.fmt(timestamp, [format])`
  
  - *Description*: Formats a timestamp into a readable string.
  
  - *Arguments*:

    - `timestamp` (Number).

    - `format` (String, optional): Default is `"%Y-%m-%d %H:%M:%S"`.
  
  - *Returns*: String.

### path - Path Module

Utilities for handling file paths cross-platform.

#### Import

```javascript
import "path";
```

#### Functions

- `path.join(part1, part2, ...)`
  
  - *Description*: Joins multiple path segments using the system separator.

- `path.base(path)`
  
  - *Returns*: The filename portion of the path.

- `path.dirname(path)`
  
  - *Returns*: The directory portion of the path.

- `path.ext(path)`
  
  - *Returns*: The file extension (including `.`).

- `path.abs(path)`
  
  - *Returns*: The absolute path.

- `path.isAbs(path)`
  
  - *Returns*: `true` if the path is absolute.

- `path.sep()`
  
  - *Returns*: The system path separator (`/` or `\`).

### glob - Glob Module

#### Import

```javascript
import "glob";
```

#### Funtions

- `glob.match(pattern, text)`
  - *Description*: Checks if the text string matches the specified pattern. It supports:
  - Wildcards: * (any sequence), ? (single char).
  - Character Sets: [a-z], [!0-9] (negation).
  - Numeric Ranges: {0..10} (inclusive).
  - Enumerations: {jpg, png, gif}.
  - Deep Matching: ** (matches across directories).
  - *Arguments*: pattern (String), text (String).
  - *Returns*: true if it matches, false otherwise.

Example:

```javascript
glob.match("*.txt", "doc.txt");            # true
glob.match("img_{0..5}.png", "img_2.png"); # true
```

#### `glob.Glob`

The `Glob` class is used to configure complex file searches. Instances of this class can be passed to `fs.list()` to retrieve matching files.

Properties:

- `Dir` (String): The root directory to start the search from. Defaults to "`.`" (current directory).
- `Pattern` (String): The glob pattern to match files against (e.g., "`*.txt`", "`**/*.c`"). Defaults to "`*`" (match all files).
- `Recursive` (Boolean): If true, the search will traverse subdirectories recursively. Defaults to false.
- `IgnoreCase` (Boolean): If true, the pattern matching and filename comparison will be case-insensitive. Defaults to false.
- `Exclude` (List): A list of glob pattern strings. Files matching any pattern in this list will be excluded from the results. Defaults to null.

Example:

To search for files, instantiate `glob.Glob`, configure its properties, and pass the object to `fs.list()`.

```javascript
import "fs";
import "glob";

var config = glob.Glob();
config.Dir = "src";
config.Pattern = "*.c";

var c_files = fs.list(config);

var search = glob.Glob();
search.Dir = ".";
search.Recursive = true;
search.Pattern = "**/*.{c,h}";        # Match C sources and headers
search.Exclude = ["vendor/*", "tmp"]; # Exclude specific directories
search.IgnoreCase = true;             # Match .C or .H too

var all_source_files = fs.list(search);
```

## Built-in Object Methods

Certain built-in types have methods attached to them automatically.

### List Methods

Available on any List object (e.g., `[1, 2]`).

- `.push(item)`: Adds `item` to the end of the list. Returns the new size.

- `.pop()`: Removes and returns the last item from the list.

- `.size()`: Returns the number of elements in the list.

### File Object Methods

Available on file objects returned by `fs.open()`.

- `.read()`: Reads the rest of the file content.

- `.readLine()`: Reads a single line from the file.

- `.write(string)`: Writes a string to the file.

- `.close()`: Closes the file handle.

### String Method

Available on any String object (e.g., `"hello"`).

- `.len()`
  
  - *Description*: Returns the length of the string.
  
  - *Returns*: Number.

- `.sub(start, [end])`
  
  - *Description*: Returns a substring from `start` index up to (but not including) `end`. Supports negative indexing (e.g., `-1` is the last character).
  
  - *Arguments*:

    - `start` (Number).

    - `end` (Number, optional): Defaults to the end of the string.
  
  - *Returns*: String.

- `.trim()`
  
  - *Description*: Removes whitespace from both the beginning and the end of the string.
  
  - *Returns*: String.

- `.upper()`
  
  - *Description*: Returns a copy of the string converted to uppercase.
  
  - *Returns*: String.

- `.lower()`
  
  - *Description*: Returns a copy of the string converted to lowercase.
  
  - *Returns*: String.

- `.find(substring)`
  
  - *Description*: Searches for the first occurrence of `substring`.
  
  - *Arguments*: `substring` (String).
  
  - *Returns*: Number (the index of the first match, or `-1` if not found).

- `.split(delimiter)`
  
  - *Description*: Splits the string into a list of substrings based on the `delimiter`. If `delimiter` is an empty string `""`, it splits the string into individual characters.
  
  - *Arguments*: `delimiter` (String).
  
  - *Returns*: List of Strings.

- `.replace(old, new)`
  
  - *Description*: Returns a new string with all occurrences of `old` replaced by `new`.
  
  - *Arguments*: `old` (String), `new` (String).
  
  - *Returns*: String.

## Embedding Cieto in C

Cieto can be embedded into a C host program. In embedded mode, the host owns the VM and decides when to load scripts, what native functions are available, how output is handled, and when the VM is destroyed.

The basic lifecycle is:

```c
CieVM* vm = cie_vm_create();

cie_vm_eval(vm, source, "<script>");
cie_vm_call(vm, "functionName", argCount, args, &result);

cie_vm_destroy(vm);
```

### Evaluating source code

`cie_vm_eval()` compiles and executes a null-terminated Cieto source string:

```c
CieStatus status = cie_vm_eval(vm,
    "func add(a, b){ return a + b; }\n",
    "<embedded>"
);
```

The source name is used only for diagnostics. It can be a real file path, but it can also be a virtual name such as `"<embedded>"`.

### Calling Cieto from C

A host program can call a global Cieto function with `cie_vm_call()`:

```c
CieValue args[] = {
    cie_value_number(20),
    cie_value_number(22)
};

CieValue result;
CieStatus status = cie_vm_call(vm, "add", 2, args, &result);
```

The initial public `CieValue` API supports `null`, `bool`, and `number` for C-to-Cieto calls and return values. String arguments can be read inside native callbacks, and strings can be returned from native callbacks to Cieto, but persistent public string ownership for `cie_vm_call()` results should be handled by a future API.

### Registering C functions

A C function can be registered as a global Cieto function:

```c
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

cie_vm_register_native(vm, "hostAdd", hostAdd, NULL);
```

After registration, Cieto code can call it like an ordinary function:

```javascript
print hostAdd(20, 22);
```

`userData` is borrowed by Cieto. The host must keep it valid for as long as the registered function may be called.

### Capturing output and errors

By default, Cieto output goes to standard output and runtime error output goes to standard error. A host can redirect both streams:

```c
static void captureWrite(const char* text, size_t length, void* userData){
    fwrite(text, 1, length, (FILE*)userData);
}

cie_vm_set_output(vm, captureWrite, stdout);
cie_vm_set_error_output(vm, captureWrite, stderr);
```

This is useful for GUI programs, servers, tests, and plugin systems where embedded scripts should not write directly to the process console.

### Error handling

Most public API functions return a `CieStatus`. When compilation or runtime execution fails, the host can read the last error message:

```c
if(status != CIE_STATUS_OK){
    const char* error = cie_vm_last_error(vm);
    fprintf(stderr, "%s\n", error != NULL ? error : cie_status_string(status));
}
```

The returned error pointer is owned by the VM. It must not be freed and remains valid until the next API call that updates the VM error state or until the VM is destroyed.

### Host safety

VMs created by `cie_vm_create()` are embedding-oriented. Script code cannot terminate the host process through `os.exit()`; this produces a normal Cieto runtime error instead. This keeps embedded scripts inside the host application's error boundary.

### Building against an installed Cieto

After installing Cieto, external programs can link against `libcieto.a`:

```sh
gcc main.c -I /path/to/cieto/include -L /path/to/cieto/lib -lcieto -lm -o embed_app
```

For CMake users, see `examples/embedding/external-cmake/`.

## Command Line

Run a script:

```sh
cieto path/to/script.cies
cieto run path/to/script.cies
```

Disable compiler optimizations for debugging or test comparison:

```sh
cieto --no-opt path/to/script.cies
```

Dump bytecode:

```sh
cieto --dump path/to/script.cies
cieto --no-opt --dump path/to/script.cies
```

## REPL

### Launch the REPL

To start the REPL, simply run the Cieto executable without any arguments in your terminal.

```bash
$ ./cieto
Cieto REPL. Press Ctrl+C to exit.
>>>
```

### Features

The Cieto REPL is powered by the `linenoise` library, offering modern terminal features:

#### Persistent History

- *Behavior*: The REPL remembers 100 lines of your previously entered commands even after you exit.

- *Storage*: History is saved to a hidden file named `.cieto_history` in the Cieto directory.

- *Navigation*: Use the **Up** and **Down** arrow keys to scroll through your command history.

#### Tab Auto-Completion

Pressing the **Tab** key triggers auto-completion for Cieto keywords.

### Exiting

To exit the REPL session, press **Ctrl+C**.
