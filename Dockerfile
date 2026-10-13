FROM gcc:13 AS build
RUN apt-get update && apt-get install -y --no-install-recommends     python3-pip git wget ca-certificates build-essential     && rm -rf /var/lib/apt/lists/*
RUN wget -q -O /tmp/cmake.tar.gz     https://github.com/Kitware/CMake/releases/download/v3.28.3/cmake-3.28.3-linux-x86_64.tar.gz     && tar -xzf /tmp/cmake.tar.gz --strip-components=1 -C /usr/local     && rm /tmp/cmake.tar.gz
RUN pip3 install --no-cache-dir --break-system-packages "conan==1.66.0"
RUN conan profile new default --detect --force     && conan profile update settings.compiler.libcxx=libstdc++11 default
WORKDIR /app
COPY conanfile.txt ./
RUN mkdir -p build && cd build && conan install ..     --build=missing -pr:h default -pr:b default     -s:h build_type=Release -s:b build_type=Release     -s:h compiler.cppstd=20 -s:b compiler.cppstd=20     -o 'boost/*:pch=False' -o 'boost/*:without_test=True'     -o 'boost/*:without_python=True' -o 'boost/*:without_stacktrace=True'     -o 'boost/*:bzip2=False'     -c 'catch2/*:tools.build:cxxflags=["-include", "cstdint"]'
COPY CMakeLists.txt ./
COPY src ./src
COPY verification ./verification
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release     -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DBUILD_TESTING=ON     && cmake --build build --parallel 1     && ctest --test-dir build --output-on-failure
FROM gcc:13 AS run
RUN groupadd -r www && useradd -r -g www -d /app -s /usr/sbin/nologin www
WORKDIR /app
COPY --from=build /app/build/bin/game_server /app/game_server
COPY data /app/data
COPY static /app/static
RUN chown -R www:www /app
USER www
EXPOSE 8080
ENTRYPOINT ["/app/game_server", "/app/data/config.json"]
