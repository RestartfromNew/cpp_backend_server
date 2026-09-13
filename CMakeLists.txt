cmake_minimum_required(VERSION 4.2.3)
project(cpp_backend_server)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PostgreSQL REQUIRED)
set(hiredis_DIR "/usr/lib/x86_64-linux-gnu/cmake/Hiredis")
find_package(redis++ CONFIG REQUIRED)
find_package(nlohmann_json REQUIRED)
add_executable(cpp_backend_server src/main.cpp
        src/server/TcpServer.cpp
        include/server/TcpServer.h
        src/databases/DatabaseConnection.cpp
        include/databases/DatabaseConnection.h
        src/repository/UserRepository.cpp
        include/repository/UserRepository.h
        src/service/UserService.cpp
        include/service/UserService.h
        src/databases/RedisClient.cpp
        include/databases/RedisClient.h
        include/domain/User.h
        #不要有重复项
        src/databases/DatabaseError.cpp
        include/databases/DatabaseError.h
        src/databases/DatabaseResult.cpp
        include/databases/DatabaseResult.h
        src/common/UniqueFd.cpp
        include/common/UniqueFd.h
        src/server/Connection.cpp
        include/server/Connection.h
        include/http/HttpRequest.h
        include/http/HttpRequest.h
        src/http/HttpParse.cpp
        include/http/HttpParse.h
        src/handler/Router.cpp
        include/handler/Router.h
        src/handler/UserHandler.cpp
        include/handler/UserHandler.h
        include/http/HttpResponse.h
        src/http/HttpResponseGenerator.cpp
        include/http/HttpResponseGenerator.h
        src/http/HttpResponse.cpp
        src/http/HttpSession.cpp
        include/http/HttpSession.h
)
target_include_directories(
        cpp_backend_server

        PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(
        cpp_backend_server
        PRIVATE
        PostgreSQL::PostgreSQL
        redis++::redis++
        nlohmann_json::nlohmann_json
)
target_compile_options(
        cpp_backend_server
        PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
)
