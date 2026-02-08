#!/bin/bash
set -e

echo "=== Verifying ALCC Toolchain ==="

echo "[1] Testing Compiler..."
./alcc-c test.lua -o test.luac

echo "[2] Testing Disassembler..."
./alcc-d test.luac > test.asm

echo "[3] Testing Assembler..."
./alcc-a test.asm -o test_new.luac

echo "[4] Testing Disassembler on new luac..."
./alcc-d test_new.luac > test_new.asm

echo "[5] Comparing assembly (ignoring headers)..."
grep -v "; Function:" test.asm > test_clean.asm
grep -v "; Function:" test_new.asm > test_new_clean.asm

if diff test_clean.asm test_new_clean.asm; then
    echo "    Assembly matches!"
else
    echo "    Assembly mismatch!"
    exit 1
fi

echo "[6] Running test_new.luac..."
../lua_source/lua test_new.luac > output.txt
if grep -q "30" output.txt; then
    echo "    Execution output matches!"
else
    echo "    Execution output mismatch!"
    exit 1
fi

echo "[7] Testing Decompiler..."
./alcc-dec test_new.luac > test.dec.lua
echo "    Decompiled successfully."

echo "[8] Testing Complex Test Case..."
./alcc-c tests/complex.lua -o complex.luac
./alcc-d complex.luac > complex.asm
./alcc-a complex.asm -o complex_new.luac
../lua_source/lua complex_new.luac > complex_output.txt
if grep -q "35" complex_output.txt && grep -q "if branch" complex_output.txt; then
    echo "    Complex test output verification passed."
else
    echo "    Complex test output verification failed!"
    cat complex_output.txt
    exit 1
fi

echo "[9] Testing Plugin System..."
./alcc-d test.luac -p plugins/sample_plugin.so > test_plugin.asm
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
./alcc-a test_error.asm -o test_error.luac > error.log 2>&1
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
