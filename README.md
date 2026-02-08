# ALCC (Assembly Lua Compiler Collection)

ALCC is a comprehensive toolchain for Lua 5.5 (work) bytecode manipulation. It provides a suite of tools for compiling, disassembling, assembling, and decompiling Lua bytecode, designed to facilitate analysis, modification, and understanding of Lua internals.

## Features

- **alcc-c**: Compiler (Source -> Bytecode) - Compiles Lua source code into bytecode.
- **alcc-d**: Disassembler (Bytecode -> Assembly) - Converts bytecode into human-readable assembly.
- **alcc-a**: Assembler (Assembly -> Bytecode) - Assembles modified assembly back into valid bytecode.
- **alcc-dec**: Decompiler (Bytecode -> Source Pseudo-code) - Reconstructs high-level Lua source code from bytecode, including control flow (if/else, loops) and variable naming.

## Architecture

The project is built with modularity in mind:
- **Core**: `src/core/alcc_backend.h` defines a generic interface for opcode handling.
- **Backend**: `src/backend/lua55.cpp` implements the interface for Lua 5.5-work.
- **Plugins**: A plugin system (`src/plugin/alcc_plugin.h`) allows extending tool functionality.

## Build Instructions

### Prerequisites

- Linux environment (e.g., Ubuntu)
- `g++` (supporting C++17)
- `make`
- `git`
- `libreadline-dev`

### Directory Structure

The build process assumes a sibling directory structure where `lua_source` is located next to `ALCC`.

```
.
├── ALCC/        (This repository)
└── lua_source/  (Lua 5.5 source code)
```

### Building

1.  **Clone this repository:**
    ```bash
    git clone https://github.com/your-username/ALCC.git
    ```

2.  **Clone Lua 5.5 source (sibling directory):**
    ```bash
    git clone https://github.com/lua/lua.git lua_source
    cd lua_source
    # Ensure you are on the correct branch/tag for Lua 5.5-work if needed
    ```

3.  **Build Lua:**
    You must build the Lua library first. Note that you may need to adjust the `makefile` in `lua_source` to remove `-march=native` for compatibility if cross-compiling, or simply run:
    ```bash
    cd lua_source
    make all
    ```

4.  **Build ALCC:**
    Navigate to the `ALCC` directory and run `make`:
    ```bash
    cd ../ALCC
    make
    ```

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
To build and use a plugin:
```bash
make plugins/sample_plugin.so
./alcc-d input.luac -p plugins/sample_plugin.so
```

## Contributing

Please read [CONTRIBUTING.md](CONTRIBUTING.md) for details on our code of conduct, and the process for submitting pull requests to us.

## License

This project is licensed under the GNU Affero General Public License v3.0 - see the [LICENSE](LICENSE) file for details.

## Copyright

Copyright (C) 2026 ALCC Contributors.
