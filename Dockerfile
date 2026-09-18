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
    && rm /tmp/cmake.tar.gz

RUN pip3 install --no-cache-dir --break-system-packages "conan==1.*"

RUN conan profile new default --detect --force && \
    conan profile update settings.compiler.libcxx=libstdc++11 default

WORKDIR /app

# --- Кэш зависимостей: копируем ТОЛЬКО манифесты ---
COPY conanfile.txt ./
COPY CMakeLists.txt ./

# Conan ставит зависимости. Если conanfile.txt не менялся — слой берётся из кэша.
#RUN mkdir -p build && cd build && conan install .. --build=missing
RUN mkdir -p build && cd build && conan install .. --build=missing && \
    echo "=== Boost targets ===" && grep -R "add_library(Boost::" -n . || true

# --- Только теперь копируем исходники ---
COPY ./src ./src/

# Сборка проекта (без conan install — он уже сделан выше)
RUN cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
#    cmake --build . --parallel "$(nproc)"
    cmake --build . --parallel 1

# --- Этап запуска (тонкий образ) ---
#FROM debian:bookworm-slim AS run
#FROM debian:trixie-slim AS run
FROM gcc:13 AS run

# libstdc++6 нужен для запуска бинарника, собранного gcc:13
RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

RUN groupadd -r www && \
    useradd -r -g www -d /app -s /usr/sbin/nologin www

COPY --from=build /app/build/bin/game_server /app/game_server
COPY ./data /app/data
COPY ./static /app/static

RUN chown -R www:www /app

USER www
WORKDIR /app

EXPOSE 8080

ENTRYPOINT ["/app/game_server", "/app/data/config.json"]
