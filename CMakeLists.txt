cmake_minimum_required(VERSION 3.15)
project(MoneyTracker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Ищем pkg-config для PostgreSQL (libpqxx)
find_package(PkgConfig REQUIRED)
pkg_check_modules(libpqxx REQUIRED libpqxx)

# 2. Скачиваем httplib и nlohmann_json через FetchContent
include(FetchContent)

FetchContent_Declare(
    cpp-httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.15.3.zip
)
FetchContent_MakeAvailable(cpp-httplib)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

# 3. Создаем исполняемый файл
add_executable(MoneyTracker 
    money_tracker.cpp
)

# 4. Подключаем все библиотеки к нашему проекту
target_link_libraries(MoneyTracker PRIVATE 
    ${libpqxx_LIBRARIES}
    httplib
    nlohmann_json::nlohmann_json
)

# Подключаем пути заголовочных файлов PostgreSQL
target_include_directories(MoneyTracker PRIVATE ${libpqxx_INCLUDE_DIRS})
target_link_directories(MoneyTracker PRIVATE ${libpqxx_LIBRARY_DIRS})