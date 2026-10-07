cmake_minimum_required(VERSION 4.2.3)
project(cpp_backend_server)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PostgreSQL REQUIRED)
set(hiredis_DIR "/usr/lib/x86_64-linux-gnu/cmake/Hiredis")
find_package(redis++ CONFIG REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Boost REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED libsodium)
find_package(jwt-cpp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
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
        src/service/RegisterService.cpp
        include/service/RegisterService.h
        src/service/LoginService.cpp
        include/service/LoginService.h
        src/service/LoginService.cpp
        src/Auth/RefreshTokenService.cpp
        include/Auth/RefreshTokenService.h
        src/Auth/AccessTokenService.cpp
        include/Auth/AccessTokenService.h
        src/repository/RefreshTokenRepository.cpp
        include/repository/RefreshTokenRepository.h
        src/handler/RefreshTokenHandler.cpp
        include/handler/RefreshTokenHandler.h
        src/http/AuthMiddleWare.cpp
        include/http/AuthMiddleWare.h
        src/databases/DatabasePool.cpp
        include/databases/DatabasePool.h
        src/databases/ConnectionLease.cpp
        include/databases/ConnectionLease.h
        src/server/ThreadPool.cpp
        include/server/ThreadPool.h
        src/websocket/WebSocketPool.cpp
        include/websocket/WebSocketPool.h
        src/service/ServiceThreadPool.cpp
        include/service/ServiceThreadPool.h
        src/websocket/WebsocketWorker.cpp
        include/websocket/WebsocketWorker.h
        src/websocket/WebSocketSession.cpp
        include/websocket/WebSocketSession.h
        src/websocket/WebSocketHandler.cpp
        include/websocket/WebSocketHandler.h
        src/service/ChatService.cpp
        include/service/ChatService.h
        src/websocket/webSocketDispatcher.cpp
        include/websocket/webSocketDispatcher.h
        include/domain/FriendshipRequestRecord.h
        src/repository/KeyRepository.cpp
        include/repository/KeyRepository.h
        src/repository/EnvelopeRepository.cpp
        include/repository/EnvelopeRepository.h
        include/domain/Device.h
        include/domain/DeviceKeyBundle.h
        src/service/KeyService.cpp
        include/service/KeyService.h
)
target_include_directories(
        cpp_backend_server

        PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${SODIUM_INCLUDE_DIRS}
)
target_link_libraries(
        cpp_backend_server
        PRIVATE
        PostgreSQL::PostgreSQL
        redis++::redis++
        nlohmann_json::nlohmann_json
        Boost::headers
        ${SODIUM_LIBRARIES}
        jwt-cpp::jwt-cpp
        OpenSSL::Crypto
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



find_package(Threads REQUIRED)
target_link_libraries(cpp_backend_server PRIVATE Threads::Threads)

add_executable(thread_pool_test
        tests/thread_pool_test.cpp
        src/server/ThreadPool.cpp
        src/server/TcpServer.cpp
        src/server/Connection.cpp
        src/common/UniqueFd.cpp
        src/http/HttpSession.cpp
        src/http/HttpParse.cpp
        src/http/HttpResponse.cpp
        src/http/HttpResponseGenerator.cpp
        src/http/AuthMiddleWare.cpp
        src/handler/Router.cpp
        src/Auth/AccessTokenService.cpp
        src/databases/DatabaseError.cpp
)
target_include_directories(thread_pool_test PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(thread_pool_test PRIVATE Threads::Threads Boost::headers
        nlohmann_json::nlohmann_json jwt-cpp::jwt-cpp)

add_executable(websocket_connection_test
        tests/websocket_connection_test.cpp
        src/server/ThreadPool.cpp
        src/server/TcpServer.cpp
        src/server/Connection.cpp
        src/common/UniqueFd.cpp
        src/http/HttpSession.cpp
        src/http/HttpParse.cpp
        src/http/HttpResponse.cpp
        src/http/HttpResponseGenerator.cpp
        src/http/AuthMiddleWare.cpp
        src/handler/Router.cpp
        src/Auth/AccessTokenService.cpp
        src/databases/DatabaseError.cpp
        src/websocket/WebSocketPool.cpp
        src/websocket/WebsocketWorker.cpp
        src/websocket/WebSocketSession.cpp
        src/websocket/WebSocketHandler.cpp
)

target_include_directories(websocket_connection_test
        PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(websocket_connection_test
        PRIVATE
        Threads::Threads
        Boost::headers
        nlohmann_json::nlohmann_json
        jwt-cpp::jwt-cpp
        OpenSSL::Crypto
)

target_compile_options(websocket_connection_test
        PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
)

add_executable(database_pool_test
        tests/data_connection_pool_main.cpp
        src/databases/DatabasePool.cpp
        src/databases/DatabaseConnection.cpp
        src/databases/DatabaseResult.cpp
        src/databases/DatabaseError.cpp
        src/databases/ConnectionLease.cpp
)

target_include_directories(database_pool_test
        PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(database_pool_test
        PRIVATE
        PostgreSQL::PostgreSQL
        Threads::Threads
)

enable_testing()
add_test(NAME thread_pool_lifecycle COMMAND thread_pool_test)
set_tests_properties(thread_pool_lifecycle PROPERTIES TIMEOUT 20)

add_test(NAME websocket_connection_lifecycle COMMAND websocket_connection_test)
set_tests_properties(websocket_connection_lifecycle PROPERTIES TIMEOUT 75)

add_test(
        NAME database_pool_smoke
        COMMAND database_pool_test
)

set_tests_properties(database_pool_smoke PROPERTIES
        TIMEOUT 20
)

