# Этап сборки
FROM gcc:13 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    python3-pip \
    git \
    wget \
    ca-certificates \
    build-essential \
    && rm -rf /var/lib/apt/lists/*

RUN wget -q -O /tmp/cmake.tar.gz \
        https://github.com/Kitware/CMake/releases/download/v3.28.3/cmake-3.28.3-linux-x86_64.tar.gz \
    && tar -xzf /tmp/cmake.tar.gz --strip-components=1 -C /usr/local \
    && rm /tmp/cmake.tar.gz \
    && cmake --version

RUN pip3 install --no-cache-dir --break-system-packages "conan==1.*"

RUN conan profile new default --detect --force && \
    conan profile update settings.compiler.libcxx=libstdc++11 default

WORKDIR /app

COPY conanfile.txt ./
COPY CMakeLists.txt ./
COPY ./src ./src/

RUN mkdir -p build && cd build && \
    conan install .. --build=missing && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    cmake --build . --parallel "$(nproc)" && \
    find . -type f -name game_server -ls

# Этап запуска
FROM gcc:13 AS run

RUN groupadd -r www && \
    useradd -r -g www -d /app -s /usr/sbin/nologin www

COPY --from=build /app/build/bin/game_server /app/game_server
COPY ./data /app/data

RUN chown -R www:www /app

USER www
WORKDIR /app

EXPOSE 8080

ENTRYPOINT ["/app/game_server", "/app/data/config.json"]
