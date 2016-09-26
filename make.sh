#!/bin/bash
GCC="g++-4.9"
LZMADIR="./third_party/lzma"
ZILIBDIR="./third_party/zi_lib"
JSONDIR="./third_party/json/src"
VMMLIBDIR="./third_party/vmmlib"

CXXINCLUDES="-I/usr/include -I./include -I$LZMADIR -I$ZILIBDIR -I$JSONDIR -I$VMMLIBDIR"
CXXLIBS="-L./lib -L/usr/lib/x86_64-linux-gnu"
COMMON_FLAGS="-fPIC -g -std=c++11"
OPTIMIZATION_FLAGS="-DNDEBUG -O3"

mkdir -p build
mkdir -p lib
mkdir -p bin
echo "Compiling LZMA decoding"
$GCC -c $COMMON_FLAGS $OPTIMIZATION_FLAGS $LZMADIR/LzmaLib.c -o build/LzmaLib.o
$GCC -c $COMMON_FLAGS $OPTIMIZATION_FLAGS $LZMADIR/LzmaDec.c -o build/LzmaDec.o
$GCC -c $COMMON_FLAGS $OPTIMIZATION_FLAGS $LZMADIR/Alloc.c -o build/Alloc.o

echo "Creating liblzma_dec.a"
ar rcs lib/liblzma_dec.a build/LzmaLib.o build/LzmaDec.o build/Alloc.o

echo "Compiling RTM"
$GCC -c $CXXINCLUDES $CXXLIBS $COMMON_FLAGS $OPTIMIZATION_FLAGS src/LZMADecode.cpp -Wl,-Bstatic -llzma_dec -o build/LZMADecode.o
$GCC -c $CXXINCLUDES $CXXLIBS $COMMON_FLAGS $OPTIMIZATION_FLAGS src/CurlObject.cpp -Wl,-Bdynamic -lcurl -o build/CurlObject.o
#$GCC -c $CXXINCLUDES $CXXLIBS $COMMON_FLAGS $OPTIMIZATION_FLAGS src/SpawnHelper.cpp -o build/SpawnHelper.o
$GCC -c $CXXINCLUDES $CXXLIBS $COMMON_FLAGS $OPTIMIZATION_FLAGS src/Volume.cpp -o build/Volume.o
$GCC -c $CXXINCLUDES $CXXLIBS $COMMON_FLAGS $OPTIMIZATION_FLAGS src/main.cpp -o build/main.o

$GCC $CXXINCLUDES $CXXLIBS $COMMON_FLAGS $OPTIMIZATION_FLAGS -o bin/spawner build/LZMADecode.o build/CurlObject.o build/Volume.o build/main.o -Wl,-Bstatic -llzma_dec -Wl,-Bdynamic -lcurl
#echo "Creating libspawner.so"
#$GCC $CXXLIBS -shared -fPIC -o lib/libspawner.so build/LZMADec.o build/spawner.o -Wl,-Bstatic -llzma_dec -Wl,-Bdynamic
