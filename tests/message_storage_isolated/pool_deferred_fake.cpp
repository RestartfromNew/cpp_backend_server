#include "service/ServiceThreadPool.h"
ServiceThreadPool::ServiceThreadPool(int count, ChatService& service):chatService(service),threadCount(count){}
void ServiceThreadPool::init(){while(!tasks_.empty()){auto task=std::move(tasks_.front());tasks_.pop();task();}}
void ServiceThreadPool::receiveTask(Task task){if(stop_)throw std::runtime_error("closed");tasks_.push(std::move(task));}
void ServiceThreadPool::close(){stop_=true;}
