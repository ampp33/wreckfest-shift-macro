# Cross-compiles the .asi plugin with MinGW-w64 and exports it laid out
# like the game folder, ready to copy into Wreckfest's install directory.
#
#   DOCKER_BUILDKIT=1 docker build --output out .
#
# Result: out/scripts/wreckfest-shift-macro.asi + wreckfest-shift-macro.toml
FROM ubuntu:24.04 AS build

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

# Export stage: `--output <dir>` copies these files to the host.
FROM scratch AS export
COPY --from=build /src/build/wreckfest-shift-macro.asi /scripts/
COPY --from=build /src/config/default.toml /scripts/wreckfest-shift-macro.toml
