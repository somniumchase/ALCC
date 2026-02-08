# ALCC - Lua 5.5 Tools

This project implements a toolchain for Lua 5.5 bytecode manipulation:
- **alcc-c**: Compiler (Source -> Bytecode)
- **alcc-d**: Disassembler (Bytecode -> Assembly)
- **alcc-a**: Assembler (Assembly -> Bytecode)
- **alcc-dec**: Decompiler (Bytecode -> Source Pseudo-code)

## Architecture
- **Core**: `src/core/alcc_opcodes.h` and `src/core/lua55_ops.c` provide an abstraction layer for Lua 5.5 opcodes, allowing easier porting to future versions.
- **Plugins**: `src/plugin/alcc_plugin.h` defines hooks for extending tool functionality.

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
