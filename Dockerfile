# Cross-compiles the .asi plugin with MinGW-w64 in a clean container, so
# you don't need a toolchain on the host. The image builds the binary;
# running it copies the result into a bind-mounted host directory.
#
#   mkdir -p build/scripts
#   docker build -t wreckfest-shift-macro-builder .
#   docker run --rm -v "$(pwd)/build/scripts:/build" wreckfest-shift-macro-builder
#
# Result: build/scripts/wreckfest-shift-macro.asi + wreckfest-shift-macro.toml
FROM ubuntu:24.04

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        cmake make g++-mingw-w64-x86-64-posix \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY cmake ./cmake
COPY src ./src
COPY third_party ./third_party
COPY config ./config

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
    && cmake --build build -j"$(nproc)"

CMD ["sh", "-c", "cp build/wreckfest-shift-macro.asi /build/ && cp config/default.toml /build/wreckfest-shift-macro.toml"]
