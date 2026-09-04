# Building Splitter on macOS and Linux

### Native Build (macOS or Linux)

Prerequisites: CMake 3.13+ and a C++17 compiler (`clang++` or `g++`).

```sh
cd splitter
cmake -B build -S .
cmake --build build
```

### Cross-compiling for Linux from macOS

To produce a static Linux binary on macOS:

1. Install `FiloSottile/musl-cross/musl-cross` via Homebrew:
   ```sh
   brew install FiloSottile/musl-cross/musl-cross
   ```
2. Build with the musl toolchain and explicit static linking:
   ```sh
   cd splitter
   cmake -B build-linux -S . \
     -DCMAKE_SYSTEM_NAME=Linux \
     -DCMAKE_C_COMPILER=x86_64-linux-musl-gcc \
     -DCMAKE_CXX_COMPILER=x86_64-linux-musl-g++ \
     -DCMAKE_EXE_LINKER_FLAGS="-static"
   cmake --build build-linux
   ```
3. Verify the resulting ELF is statically linked:
   ```sh
   file build-linux/splitter
   # Expected output contains: ELF 64-bit LSB executable, ..., statically linked
   ```