#!/bin/bash
set -e

echo "=== Verifying ALCC Toolchain for AndroLua 5.3.3 ==="

# Setup AndroLua Source
echo "[Setup] Preparing AndroLua 5.3.3 source..."
if [ ! -d "../androlua533_source" ]; then
    echo "    Cloning androlua5.3.3..."
    git clone https://github.com/OxenFxc/androlua5.3.3.git ../androlua533_source
else
    echo "    androlua533_source already exists."
fi

# Apply patch if needed
if grep -q "float.h" ../androlua533_source/lmathlib.c; then
    echo "    Patch already applied."
else
    echo "    Applying patch..."
    if [ -f "patches/androlua533_support.patch" ]; then
        (cd ../androlua533_source && patch -p1 < ../ALCC/patches/androlua533_support.patch) || { echo "Patch failed!"; exit 1; }
    else
        echo "    Patch file not found: patches/androlua533_support.patch"
        exit 1
    fi
fi

# Clean and Build
echo "[0] Building tools..."
make clean > /dev/null
make LUA_VER=5.3.3 > build.log 2>&1 || { echo "Build failed! See build.log"; cat build.log; exit 1; }
echo "    Build successful."

# Generate test files
cat <<EOF > test.lua
local a = 15
local b = a
print(a + b)
EOF

cat <<EOF > test_error.asm
; Function: 0x0
; NumParams: 0, IsVararg: 0, MaxStackSize: 2
; Upvalues (0):
; Constants (0):
; Code (1):
[001] BAD_OPCODE 0 0 0
EOF

SUFFIX="-5.3.3"
LUA_BIN="../androlua533_source/lua"

echo "[1] Testing Compiler..."
./alcc-c$SUFFIX test.lua -o test.luac

echo "[2] Testing Disassembler..."
./alcc-d$SUFFIX test.luac > test.asm

echo "[3] Testing Assembler..."
./alcc-a$SUFFIX test.asm -o test_new.luac

echo "[4] Testing Disassembler on new luac..."
./alcc-d$SUFFIX test_new.luac > test_new.asm

echo "[5] Comparing assembly (ignoring headers)..."
# Remove lines starting with "; Function:" (pointer addresses differ)
# Remove comments at the end of lines (debug info might be lost)
sed '/^; Function:/d; s/;.*//' test.asm > test_clean.asm
sed '/^; Function:/d; s/;.*//' test_new.asm > test_new_clean.asm

if diff -w test_clean.asm test_new_clean.asm; then
    echo "    Assembly matches!"
else
    echo "    Assembly mismatch!"
    exit 1
fi

echo "[6] Running test_new.luac..."
$LUA_BIN test_new.luac > output.txt
if grep -q "30" output.txt; then
    echo "    Execution output matches!"
else
    echo "    Execution output mismatch!"
    cat output.txt
    exit 1
fi

echo "[7] Testing Decompiler..."
./alcc-dec$SUFFIX test_new.luac > test.dec.lua
echo "    Decompiled successfully."

echo "[8] Testing Complex Test Case..."
if [ -f tests/complex.lua ]; then
    ./alcc-c$SUFFIX tests/complex.lua -o complex.luac
    ./alcc-d$SUFFIX complex.luac > complex.asm
    ./alcc-a$SUFFIX complex.asm -o complex_new.luac
    $LUA_BIN complex_new.luac > complex_output.txt
    if grep -q "35" complex_output.txt && grep -q "if branch" complex_output.txt; then
        echo "    Complex test output verification passed."
    else
        echo "    Complex test output verification failed!"
        cat complex_output.txt
        exit 1
    fi
else
    echo "    tests/complex.lua not found, skipping."
fi

echo "[9] Testing Assembler Error Handling..."
set +e
./alcc-a$SUFFIX test_error.asm -o test_error.luac > error.log 2>&1
RET=$?
set -e
if [ $RET -ne 0 ]; then
    echo "    Assembler correctly failed."
    if grep -q "Unknown opcode: BAD_OPCODE" error.log; then
        echo "    Error message correct: Unknown opcode"
    else
        echo "    Unexpected error message:"
        cat error.log
        exit 1
    fi
else
    echo "    Assembler failed to report error!"
    exit 1
fi

echo "=== Verification Successful! ==="
