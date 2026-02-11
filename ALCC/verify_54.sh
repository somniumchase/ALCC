#!/bin/bash
set -e

echo "=== Verifying ALCC Toolchain (Lua 5.4) ==="

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

echo "[1] Testing Compiler..."
./alcc-c-5.4 test.lua -o test.luac

echo "[2] Testing Disassembler..."
./alcc-d-5.4 test.luac > test.asm

echo "[3] Testing Assembler..."
./alcc-a-5.4 test.asm -o test_new.luac

echo "[4] Testing Disassembler on new luac..."
./alcc-d-5.4 test_new.luac > test_new.asm

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
../lua54_source/src/lua test_new.luac > output.txt
if grep -q "30" output.txt; then
    echo "    Execution output matches!"
else
    echo "    Execution output mismatch!"
    exit 1
fi

echo "[7] Testing Decompiler..."
./alcc-dec-5.4 test_new.luac > test.dec.lua
echo "    Decompiled successfully."

echo "[8] Testing Complex Test Case..."
./alcc-c-5.4 tests/complex.lua -o complex.luac
./alcc-d-5.4 complex.luac > complex.asm
./alcc-a-5.4 complex.asm -o complex_new.luac
../lua54_source/src/lua complex_new.luac > complex_output.txt
if grep -q "35" complex_output.txt && grep -q "if branch" complex_output.txt; then
    echo "    Complex test output verification passed."
else
    echo "    Complex test output verification failed!"
    cat complex_output.txt
    exit 1
fi

echo "[9] Testing Plugin System..."
./alcc-d-5.4 test.luac -p plugins/sample_plugin.so > test_plugin.asm
if grep -q "\[PLUGIN\] MOVE" test_plugin.asm; then
    echo "    Plugin hooks verified!"
else
    echo "    Plugin hooks failed!"
    cat test_plugin.asm
    exit 1
fi
if grep -q "\[SAMPLE PLUGIN\] Loaded function" test_plugin.asm; then
    echo "    Plugin post_load verified!"
else
    echo "    Plugin post_load failed!"
    exit 1
fi

echo "[10] Testing Assembler Error Handling..."
set +e
./alcc-a-5.4 test_error.asm -o test_error.luac > error.log 2>&1
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
