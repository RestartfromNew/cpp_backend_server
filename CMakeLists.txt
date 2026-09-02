cmake_minimum_required(VERSION 4.2.3)
project(cpp_backend_server)

set(CMAKE_CXX_STANDARD 20)

add_executable(cpp_backend_server src/main.cpp
        src/server/TcpServer.cpp
        include/TcpServer.h)
target_include_directories(
        cpp_backend_server

        PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)