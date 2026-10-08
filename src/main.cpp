#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <sodium.h>
#include <stdexcept>
#include "common/UniqueFd.h"
#include "handler/UserHandler.h"
#include "handler/ChatHandler.h"
#include "http/HttpParse.h"
#include "handler/Router.h"
#include "repository/UserRepository.h"
#include "handler/RefreshTokenHandler.h"
#include "server/Connection.h"
#include "server/TcpServer.h"
#include "service/UserService.h"
#include "http/HttpResponseGenerator.h"
#include <cstdlib>
#include "http/HttpSession.h"
#include "service/RegisterService.h"
#include "service/LoginService.h"
#include "Auth/RefreshTokenService.h"
#include "Auth/AccessTokenService.h"
#include "http/AuthMiddleWare.h"
#include "repository/RefreshTokenRepository.h"
#include "databases/DatabasePool.h"
#include "server/ThreadPool.h"
#include <cerrno>
#include <csignal>
#include <pthread.h>
#include <websocket/WebSocketPool.h>
#include <sys/signalfd.h>
#include <unistd.h>
#include <system_error>
#include "websocket/WebSocketDispatcher.h"
#include "service/ServiceThreadPool.h"
namespace {
// Block before starting ANY background thread. Main consumes signals as data;
// no mutex, logging, or join is executed inside a signal handler.
class StopSignals {
public:
    StopSignals() {
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGTERM);
        const int error = pthread_sigmask(SIG_BLOCK, &mask, &previous_);
        if (error) throw std::system_error(error, std::generic_category(), "block stop signals");
        fd_.reset(signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC));
        if (!fd_.valid()) {
            const int saved = errno;
            pthread_sigmask(SIG_SETMASK, &previous_, nullptr);
            throw std::system_error(saved, std::generic_category(), "signalfd");
        }
    }
    ~StopSignals() { pthread_sigmask(SIG_SETMASK, &previous_, nullptr); }
    bool requested() {
        signalfd_siginfo info{};
        const auto count = ::read(fd_.get(), &info, sizeof(info));
        if (count == static_cast<ssize_t>(sizeof(info))) {
            std::cout << "[server] stop signal " << info.ssi_signo << " received\n";
            return true;
        }
        if (count < 0 && (errno == EAGAIN || errno == EINTR)) return false;
        throw std::runtime_error("Failed to read stop signal");
    }
private:
    sigset_t previous_{};
    UniqueFd fd_;
};
} // namespace

int main()
{
    try {
        StopSignals stopSignals; // Destroyed last, after all background threads.
        std::cout << std::unitbuf;
        //读取配置
        if (sodium_init() < 0) {
            throw std::runtime_error("Failed to initialize libsodium");
        }
        const char* databaseUrl = std::getenv("DATABASE_URL");
        if (databaseUrl == nullptr || databaseUrl[0] == '\0') {
            throw std::runtime_error("DATABASE_URL environment variable is not set or is empty");
        }
        //组装组件
        DatabasePoolConfig config{};
        config.min_connections_ = 4;
        config.max_connections = 8;
        config.acquire_time = 5000;
        config.idle_time = 60000;
        config.check_interval = 100;
        DatabasePool databasePool;
        if (!databasePool.init(databaseUrl, config)) {
            throw std::runtime_error("Concurrent pool initialization failed");
        }
        UserRepository userRepository{databasePool};
        RefreshTokenRepository refreshTokenRepository{databasePool};
        RefreshTokenService refreshTokenService{refreshTokenRepository};
        UserService userService{userRepository};
        RegisterService registerService{userRepository};



        const char* secret=std::getenv("JWT_SECRET");
        if (secret == nullptr || secret[0] == '\0') {
            throw std::runtime_error("JWT_SECRET is not set or is empty");
        }
        AccessTokenService accessTokenService{secret};
        RefreshTokenHandler refreshTokenHandler{refreshTokenService,accessTokenService};

        AuthMiddleWare authMiddleWare{accessTokenService};
        LoginService loginService{userRepository,refreshTokenService,accessTokenService};
        KeyRepository keyRepository{databasePool};
        KeyService keyService{keyRepository,userRepository};
        UserHandler userHandler{userService,registerService,loginService,keyService};
        ChatHandler chatHandler{keyService};
        Router router{authMiddleWare};
        router.addProtectedRoute(
            HttpMethod::POST,
            "/register_device",
            [&chatHandler](const HttpRequest& request, const boost::uuids::uuid& user_id)->HttpResponse {
                return chatHandler.registerNewDevice(request, user_id);
            }
        );
        router.addProtectedRoute(
            HttpMethod::POST,
            "/upload_one_time_prekeys",
            [&chatHandler](const HttpRequest& request, const boost::uuids::uuid& user_id)->HttpResponse {
                return chatHandler.uploadOneTimePrekeys(request, user_id);
            }
        );
        router.addProtectedRoute(
            HttpMethod::POST,
            "/get_one_time_prekey",
            [&chatHandler](const HttpRequest& request, const boost::uuids::uuid& user_id)->HttpResponse {
                return chatHandler.getOneTimePrekey(request, user_id);
            }
        );
        router.addProtectedRoute(
            HttpMethod::POST,
            "/friend_devices",
            [&chatHandler](const HttpRequest& request, const boost::uuids::uuid& user_id)->HttpResponse {
                return chatHandler.fetchFriendDevices(request, user_id);
            }
        );
        //注册路由，如果方法为Get,路径为path,就调用userHanler.getUser方法
        router.addRoute(
            HttpMethod::GET,
            "/refresh_token",
            [&refreshTokenHandler](const HttpRequest& request)->HttpResponse {
                //返回一个HttpResponse
                return refreshTokenHandler.VerifyRefreshToken(request);
            }
            );
        router.addRoute(
            HttpMethod::GET,
            "/user",
            [&userHandler](const HttpRequest& request)->HttpResponse {
                //返回一个HttpResponse
                return userHandler.getUser(request);
            }
        );
        router.addProtectedRoute(
            HttpMethod::POST,
           "/verify_access_token",
           [&userHandler](const HttpRequest& request,const boost::uuids::uuid& uuid)->HttpResponse {
               HttpResponse response =
               userHandler.verifyaccess(request);
               response.headers["Content-Type"] =
               "application/json";
               return response;
           }
            );
        router.addProtectedRoute(
           HttpMethod::GET,
          "/find_user_by_username",
          [&userHandler](const HttpRequest& request,const boost::uuids::uuid& uuid)->HttpResponse {
              HttpResponse response =userHandler.FindUserByUsername(request,uuid);
              response.headers["Content-Type"] ="application/json";
              return response;
          }
           );
        router.addProtectedRoute(
           HttpMethod::GET,
          "/fetch_unprocessed_friend_request",
          [&userHandler](const HttpRequest& request,const boost::uuids::uuid& uuid)->HttpResponse {
              HttpResponse response =userHandler.FetchUnprocessedFriendship(request,uuid);
              response.headers["Content-Type"] ="application/json";
              return response;
          }
           );
        router.addProtectedRoute(
           HttpMethod::POST,
          "/request_friendship",
          [&userHandler](const HttpRequest& request,const boost::uuids::uuid& uuid)->HttpResponse {
              HttpResponse response =userHandler.RequestFriendship(request,uuid);
              response.headers["Content-Type"] ="application/json";
              return response;
          }
           );
        router.addRoute(
            HttpMethod::POST,
            "/register_by_email",
            [&userHandler](const HttpRequest& request)->HttpResponse {

                //返回一个HttpResponse
                HttpResponse response=userHandler.Register(request);
                response.headers["Content-Type"] = "application/json";
                return response;
            }
            );
        router.addRoute(
            HttpMethod::POST,
            "/login_by_email",
            [&userHandler](const HttpRequest& request)->HttpResponse {
                //返回一个HttpResponse
                HttpResponse response=userHandler.LoginByEmail(request);
                response.headers["Content-Type"] = "application/json";
                return response;
            }
            );
        router.addProtectedRoute(
            HttpMethod::POST,
            "/process_friendship_request",
            [&userHandler](const HttpRequest& request, const boost::uuids::uuid& uuid) {
                return userHandler.ProcessFriendshipRequest(request, uuid);
            }
        );
        router.addProtectedRoute(
            HttpMethod::GET,
            "/fetch_friends",
            [&userHandler](const HttpRequest& request, const boost::uuids::uuid& uuid)->HttpResponse {
                HttpResponse response = userHandler.FetchFriends(request, uuid);
                response.headers["Content-Type"] = "application/json";
                return response;
            }
        );

        //启动服务
        TcpServer server{"0.0.0.0",8081};
        //TcpServer server{"0.0.0.0",8082};
        server.start();
        ThreadPoolConfig thread_pool_config{4,8};
        WebSocketPoolConfig web_socket_pool_config{4,100};
        ServiceThreadPool service_thread_pool(2);
        WebSocketDispatcher web_socket_dispatcher(service_thread_pool);
        WebSocketPool web_socket_pool(web_socket_pool_config,web_socket_dispatcher);
        ThreadPool thread_pool(thread_pool_config,server,router,web_socket_pool);
        service_thread_pool.init();
        if (!thread_pool.init())
            throw std::runtime_error("Thread pool initialization failed");
        if (!web_socket_pool.init())
            throw std::runtime_error("websocket pool initialization failed");
        std::cout << "[server] ready on port 8081; workers=4; queue_capacity=8; Ctrl+C to stop\n";
        thread_pool.running([&stopSignals] { return stopSignals.requested(); });
        server.stop();
        std::cout << "[server] listener closed; waiting for workers\n";
        thread_pool.close();
        service_thread_pool.close();
        web_socket_pool.close();
        std::cout << "[server] all workers joined; closing database pool\n";
        databasePool.close();
        std::cout << "[server] database pool closed; shutdown complete\n";

    } catch (const std::exception& error) {
        std::cerr
            << "Server error: "
            << error.what()
            << '\n';
        return 1;
    }
}
