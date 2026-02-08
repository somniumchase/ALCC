#!/bin/bash
set -e
echo "Compiling test.lua..."
./alcc-c test.lua -o test.luac

echo "Disassembling test.luac..."
./alcc-d test.luac > test.asm

echo "Assembling test.asm..."
./alcc-a test.asm -o test_new.luac

echo "Disassembling test_new.luac..."
./alcc-d test_new.luac > test_new.asm

echo "Comparing assembly (ignoring headers with pointers)..."
# We ignore lines starting with ; Function: because pointers differ.
grep -v "; Function:" test.asm > test_clean.asm
grep -v "; Function:" test_new.asm > test_new_clean.asm

if diff test_clean.asm test_new_clean.asm; then
    echo "Assembly matches!"
else
    echo "Assembly mismatch!"
    exit 1
fi

echo "Running test_new.luac..."
../lua_source/lua test_new.luac > output.txt
if grep -q "30" output.txt; then
    echo "Execution output matches!"
else
    echo "Execution output mismatch!"
    exit 1
fi

echo "Decompiling test_new.luac..."
./alcc-dec test_new.luac > test.dec.lua
echo "Decompiled output:"
cat test.dec.lua

echo "Verification Successful!"
