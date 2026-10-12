#include "service/ServiceThreadPool.h"
ServiceThreadPool::ServiceThreadPool(int count,ChatService& service):chatService(service),threadCount(count){}
void ServiceThreadPool::init(){}
void ServiceThreadPool::close(){stop_=true;}
void ServiceThreadPool::receiveTask(Task task){if(stop_)throw std::runtime_error("closed");task();}
