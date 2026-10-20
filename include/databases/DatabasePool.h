//
// Created by yangb on 2026/9/21.
//

#ifndef CPP_BACKEND_SERVER_DATABASEPOOL_H
#define CPP_BACKEND_SERVER_DATABASEPOOL_H
#include <queue>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <stdexcept>
#include <chrono>
#include <iostream>
#include <databases/DatabaseConnection.h>
/** @brief 连接数、借用等待、空闲回收与巡检间隔配置；各时间字段按当前实现使用毫秒。 */
struct DatabasePoolConfig {
    int min_connections_;
    int max_connections;
    int acquire_time;
    int idle_time;
    int check_interval;
};
/** @brief 互斥保护的 PostgreSQL 连接池；借出独占连接，归还前 reset，close 等待租约归还后释放资源。 */
class DatabasePool {
public:
    static DatabasePool* get_instance() {
        static DatabasePool instance;
        return &instance;
    };
    DatabasePool()=default;
    ~DatabasePool(){close();}
    DatabasePool(DatabasePool const&)=delete;
    DatabasePool& operator=(DatabasePool const&)=delete;
    bool init(const std::string & DatabaseUrl, const DatabasePoolConfig & config);
    void releaseReturnedConnections(std::unique_ptr<DatabaseConnection> connection);
    void close();
    std::unique_ptr<DatabaseConnection> getConnection();
private:
    DatabasePoolConfig config_;
    std::string DatabaseURL;
    std::queue<std::unique_ptr<DatabaseConnection>> idle_queue;
    int total_connections_=0;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread main_thread;
    bool is_running=false;
    bool initialized_ = false;
    void startMaintenanceThread();
};


#endif //CPP_BACKEND_SERVER_DATABASEPOOL_H
