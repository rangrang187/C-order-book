# low_latency

A minimal C++20 project built with CMake.

## Requirements

- CMake 3.20 or newer
- A C++ compiler with C++20 support

## Configure and build

From the project root:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

## Run

On Windows with a multi-configuration generator:

```powershell
.\build\Release\low_latency.exe
```

On single-configuration generators:

```powershell
.\build\low_latency.exe
```

Expected output:

```text
low_latency C++20 project is ready
```

## Test

```powershell
ctest --test-dir build -C Release --output-on-failure
```

## Project structure

```text
.
|-- CMakeLists.txt
|-- README.md
`-- src
    `-- main.cpp
```

## CMake options

The project keeps generated files in `build/`, which is ignored by Git. The language standard is enforced with both `CMAKE_CXX_STANDARD` and `target_compile_features`.
