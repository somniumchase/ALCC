# ALCC - Lua 5.5 Tools

This project implements a toolchain for Lua 5.5 bytecode manipulation:
- **alcc-c**: Compiler (Source -> Bytecode)
- **alcc-d**: Disassembler (Bytecode -> Assembly)
- **alcc-a**: Assembler (Assembly -> Bytecode)
- **alcc-dec**: Decompiler (Bytecode -> Source Pseudo-code)

## Architecture
- **Core**: `src/core/alcc_backend.h` defines a generic interface for opcode handling.
- **Backend**: `src/backend/lua55.c` implements the interface for Lua 5.5, encapsulating version-specific logic. This allows easy extension to future Lua versions (e.g. 5.6) by adding a new backend.
- **Plugins**: `src/plugin/alcc_plugin.h` defines hooks for extending tool functionality (instruction printing, header analysis, assembly line modification, decompilation).

## Building
```bash
make
```
Note: Requires `../lua_source` to be built (run `make` inside `lua_source`).

## Usage
### Compiler
```bash
./alcc-c input.lua -o output.luac
```

### Disassembler
```bash
./alcc-d input.luac > output.asm
```

### Assembler
```bash
./alcc-a input.asm -o output.luac
```

### Decompiler
```bash
./alcc-dec input.luac > output.lua
```
The decompiler generates pseudo-code with the following features:
- **Variable Naming**: Uses debug info to resolve local variable names.
- **Control Flow**: Reconstructs `if ... then ... end` and loops (`for`, `while`) with indentation.
- **Expressions**: Prints arithmetic and bitwise operations in infix notation.
- **Inline Functions**: Recursively prints nested function definitions.

### Plugin System
The disassembler supports plugins to customize output.
To build the sample plugin:
```bash
make plugins/sample_plugin.so
```
To run with plugin:
```bash
./alcc-d input.luac -p plugins/sample_plugin.so
```

## Testing
Run `./verify_v2.sh`.
