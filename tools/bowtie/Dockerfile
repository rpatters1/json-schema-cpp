# SPDX-FileCopyrightText: 2026 Robert Patterson
# SPDX-License-Identifier: MIT
#
# The Bowtie harness image. Build from the repository root:
#   docker build -f tools/bowtie/Dockerfile -t localhost/cpp-json-schema-cpp .

FROM alpine:3.22 AS build
RUN apk add --no-cache g++ cmake ninja git
WORKDIR /src
COPY . .
RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DJSONSCHEMA_BUILD_TESTS=OFF -DJSONSCHEMA_BUILD_CLI=OFF -DJSONSCHEMA_BUILD_BOWTIE=ON -DJSONSCHEMA_INSTALL=OFF \
        -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
    && cmake --build /build --target jsonschema_bowtie_harness

FROM alpine:3.22
COPY --from=build /build/bin/jsonschema_bowtie_harness /usr/local/bin/harness
CMD ["/usr/local/bin/harness"]
