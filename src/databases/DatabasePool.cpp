//
// Created by yangb on 2026/9/21.
//

#include "databases/DatabasePool.h"
bool DatabasePool::init(const std::string & DatabaseUrl, const DatabasePoolConfig & config) {
    std::unique_lock<std::mutex> lock{mutex_};
    if (initialized_)
        return false;
    this->DatabaseURL= DatabaseUrl;
    if (config.min_connections_<0||config.max_connections<=0||
        config.min_connections_>config.max_connections)
        throw std::invalid_argument("Invalid DatabasePoolConfig");
    config_= config;
    std::queue<std::unique_ptr<DatabaseConnection>> pending;
    for (int i=1;i<=config.min_connections_;i++) {
        auto  connection=std::make_unique<DatabaseConnection>(DatabaseUrl);
        connection->setLastActiveTime();
        pending.push(std::move(connection));
    }
    idle_queue.swap(pending);
    total_connections_ = config.min_connections_;
    is_running = true;
    try {
        startMaintenanceThread();
    }
    catch (...) {
        is_running = false;
        total_connections_ = 0;
        idle_queue.swap(pending);
        throw;
    }
    initialized_=true;
    return true;
}
void DatabasePool::startMaintenanceThread() {
    main_thread=std::thread([this]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.check_interval));
            std::unique_lock<std::mutex> lock{mutex_};
            if (!is_running)
                break;
            int exceed_number_of_connections=total_connections_-config_.min_connections_;
            if (exceed_number_of_connections>0) {
                std::unique_ptr<DatabaseConnection> temp;
                for (std::size_t i = 0; i < idle_queue.size(); ++i) {
                    //while会一直持有池锁
                    temp=std::move(idle_queue.front());
                    idle_queue.pop();
                    const auto now = std::chrono::steady_clock::now();
                    const auto idle_duration = now -temp->getLastActiveTime();
                    if (exceed_number_of_connections>0&&idle_duration >= std::chrono::milliseconds{config_.idle_time}) {
                       temp->close();
                       exceed_number_of_connections--;
                       total_connections_--;
                    }else {
                       idle_queue.push(std::move(temp));
                   }
                }
            }
            const auto count = idle_queue.size();
            for (std::size_t i = 0; i < count; ++i) {
                std::unique_ptr<DatabaseConnection> temp;
                temp=std::move(idle_queue.front());
                idle_queue.pop();
                if (!temp->isAlive()) {
                    auto newConn=std::make_unique<DatabaseConnection>(this->DatabaseURL);
                    if (!newConn->isAlive()) {
                        total_connections_--;
                        newConn->close();
                    }
                    else {
                        newConn->setLastActiveTime();
                    idle_queue.push(std::move(newConn));
                    }
                }
                else {
                    idle_queue.push(std::move(temp));
                }

            }
            if (total_connections_<config_.min_connections_) {
                auto newConn=std::make_unique<DatabaseConnection>(this->DatabaseURL);
                if (newConn->isAlive()) {
                    newConn->setLastActiveTime();
                    idle_queue.push(std::move(newConn));
                    total_connections_++;
                }
                else {
                    break;
                }
            }

        }
    });
}

std::unique_ptr<DatabaseConnection> DatabasePool::getConnection() {
    std::unique_lock<std::mutex> lock{mutex_};
    const bool ready = cv_.wait_for(lock,std::chrono::milliseconds{config_.acquire_time},[this] {
        return !is_running|| !idle_queue.empty()|| total_connections_ < config_.max_connections;
    });
    if (!is_running) {
        throw std::runtime_error("Connection pool is closed");
    }
    if (!ready) {
        throw std::runtime_error("Timeout acquiring connection");
    }
    if (!idle_queue.empty()) {
        auto temp=std::move(idle_queue.front());
        idle_queue.pop();
        return temp;
    }
    total_connections_++;
    lock.unlock();
    try {

        auto newConn=std::make_unique<DatabaseConnection>(this->DatabaseURL);
        lock.lock();
        if (!is_running) {
            newConn->close();
            cv_.notify_all();
            throw std::runtime_error("Connection pool is closed");
        }
        return newConn;


    }
    catch (...) {
        if (!lock.owns_lock()) {
            lock.lock();
        }
        total_connections_--;
        lock.unlock();
        cv_.notify_one();
        throw;
    }
}

void DatabasePool::releaseReturnedConnections(std::unique_ptr<DatabaseConnection> connection) {
    if (!connection)
        return;
    bool result=connection->reset();
    std::unique_lock<std::mutex> lock{mutex_};
    if (!is_running) {
        connection->close();
        total_connections_--;
        cv_.notify_all();
        return;
    }
    if (!result) {
        total_connections_--;
        cv_.notify_one();
        return ;
    }
    try {
        idle_queue.push(std::move(connection));
    }
    catch (...) {
        lock.unlock();
        connection->close(); // 入队失败，销毁连接
        lock.lock();
        --total_connections_;
        lock.unlock();
        cv_.notify_all();
        return; // 处理完失败，不再向析构函数抛异常
    }

    lock.unlock();
    cv_.notify_one();

}

void DatabasePool::close() {
    //清空空闲队列
    std::unique_lock<std::mutex> lock{mutex_};
    is_running=false;
    cv_.notify_all();
    while (!idle_queue.empty()) {
        std::unique_ptr<DatabaseConnection> temp;
        temp=std::move(idle_queue.front());
        idle_queue.pop();
        temp->close();
        total_connections_--;
    }
    cv_.wait(lock, [this] {
        return total_connections_ == 0;
    });
    lock.unlock();
    if (main_thread.joinable()) {
        main_thread.join();
    }
}
