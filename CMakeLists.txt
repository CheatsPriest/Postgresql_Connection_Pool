cmake_minimum_required(VERSION 3.10)

# Принудительное указание toolchain для VS
set(CMAKE_TOOLCHAIN_FILE "C:/package/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain" FORCE)

project("Postgresql_Connection_Pool")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Явное указание путей vcpkg
set(CMAKE_PREFIX_PATH "C:/package/vcpkg/installed/x64-windows" ${CMAKE_PREFIX_PATH})

find_package(libpqxx CONFIG REQUIRED)

add_executable(Postgresql_Connection_Pool 
    "Postgresql_Connection_Pool.cpp" 
    "Postgresql_Connection_Pool.h"
)

target_link_libraries(Postgresql_Connection_Pool PRIVATE libpqxx::pqxx)