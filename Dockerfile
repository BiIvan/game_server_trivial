# Этап сборки
FROM gcc:11.3 AS build

RUN apt update && apt install -y \
    python3-pip \
    git \
    wget \
    build-essential \
    && rm -rf /var/lib/apt/lists/*

# Ставим свежий CMake (3.28.3 — точно выше 3.20)
RUN wget -O cmake.tar.gz https://github.com/Kitware/CMake/releases/download/v3.28.3/cmake-3.28.3-linux-x86_64.tar.gz && \
    tar -xzf cmake.tar.gz --strip-components=1 -C /usr/local && \
    rm cmake.tar.gz

# Conan
RUN pip3 install "conan==1.*"

WORKDIR /app
COPY conanfile.txt .
COPY CMakeLists.txt .
COPY ./src src

# Сборка
RUN mkdir -p build && cd build && \
    conan install .. --build=missing && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    cmake --build .

# Этап рантайма
FROM ubuntu:22.04 AS run

# Создаём пользователя
RUN groupadd -r www && useradd -r -g www www

# Копируем бинарник (путь исправлен: он лежит прямо в build/)
COPY --from=build /app/build/bin/game_server /app/
COPY ./data /app/data

# Права на запуск
RUN chown -R www:www /app

USER www
WORKDIR /app

# Запуск
ENTRYPOINT ["/app/game_server", "/app/data/config.json"]
