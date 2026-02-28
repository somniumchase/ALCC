#!/bin/bash
source ./emsdk/emsdk_env.sh
cd ALCC
make LUA_VER=5.5 web CXX=emcc CC=emcc AR=emar RANLIB=emranlib
