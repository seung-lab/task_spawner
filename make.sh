#!/bin/bash
GCC="g++-5"
ZILIBDIR="./third_party/zi_lib"
JSONDIR="./third_party/json/src"
VMMLIBDIR="./third_party/vmmlib"

CXXINCLUDES="-I/usr/include -I./include -I$ZILIBDIR -I$JSONDIR -I$VMMLIBDIR"
CXXLIBS="-L./lib -L/usr/lib -L/usr/lib/x86_64-linux-gnu -L/lib/x86_64-linux-gnu"
COMMON_FLAGS="-fPIC -g -std=c++11"
OPTIMIZATION_FLAGS="-DNDEBUG -O3"

mkdir -p build
mkdir -p lib
mkdir -p bin

echo "Compiling Spawner"
$GCC -c $CXXINCLUDES $CXXLIBS $COMMON_FLAGS $OPTIMIZATION_FLAGS src/Volume.cpp -o build/Volume.o
$GCC -c $CXXINCLUDES $CXXLIBS $COMMON_FLAGS $OPTIMIZATION_FLAGS src/SpawnerWrapper.cpp -o build/SpawnerWrapper.o

#$GCC -c $CXXINCLUDES $CXXLIBS $COMMON_FLAGS $OPTIMIZATION_FLAGS src/test.cpp -o build/test.o
#$GCC $CXXINCLUDES $CXXLIBS $COMMON_FLAGS $OPTIMIZATION_FLAGS -o bin/test build/Volume.o build/test.o

$GCC -c $CXXINCLUDES $CXXLIBS $COMMON_FLAGS $OPTIMIZATION_FLAGS src/SpawnSetGenerator.cpp -o build/SpawnSetGenerator.o
$GCC -c $CXXINCLUDES $CXXLIBS $COMMON_FLAGS $OPTIMIZATION_FLAGS res/spawnset.pb.cc -o build/spawnset.pb.o
#$GCC $CXXINCLUDES $CXXLIBS $COMMON_FLAGS $OPTIMIZATION_FLAGS -o bin/spawnsetgenerator build/Volume.o build/spawnset.pb.o build/SpawnSetGenerator.o -l:libprotobuf.a

#echo "Creating libspawner.so"
$GCC $CXXLIBS -shared -fPIC -o lib/libspawner.so build/Volume.o build/SpawnerWrapper.o

$GCC $CXXLIBS -shared -fPIC -o lib/spawnsetgenerator.so build/Volume.o build/spawnset.pb.o build/SpawnSetGenerator.o -l:libprotobuf.a
