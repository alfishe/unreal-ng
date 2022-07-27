Fully re-engineered Unreal Speccy emulator

Cross-platform emulation core:
- Windows 32-bit
- Windows 64-bit
- Linux 32/64-bit
- macOS 64-bit

Modular architecture:
- core - all emulation logic


Pre-requisites:
- CMake v3.16 or newer

Submodules:
- Google Test
- Google Benchmark

# How to start:

    git clone --recurse-submodules https://github.com/alfishe/unreal-ng

or

    git clone https://github.com/alfishe/unreal-ng
    git submodule init
    git submodule update


Updates:

    git pull --recurse-submodules
    
# Build

## Linux / macOS

    mkdir build
    cd build
    cmake ..
    cmake --build . --parallel 12

/src /tests /benchmarks can be built separately same way

## Windows

CMake/MSBuild chain under windows behaves weirdly: it generates MSVC projects and always uses Debug configuration totally ignoring CMAKE_BUILD_TYPE value. So the only way to have control over build type - to use cmake --config parameter.
 
    --config Debug
    --config Release

The rest is similar to *nix:

    mkdir build
    cd build
    cmake ..
    cmake --build . --config Release

/src /tests /benchmarks can be built separately same way
