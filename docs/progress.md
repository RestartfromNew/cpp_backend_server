# 进度统计
阶段 0：整理工程和建立可验证基线

阶段 1：完成底层资源管理

阶段 2：实现 Connection

阶段 3：实现最小 HTTP 模型和解析

阶段 4：实现 Router 和 Handler

阶段 5：打通 User 查询的完整请求

阶段 6：加入测试和错误映射

阶段 7：加入 Redis 缓存

阶段 8：加入线程池

阶段 9：再学习 epoll/Reactor


## Connection类
职责：负责数据库的具体执行，包括连接、执行、关闭、释放
所有权：不可以复制，但是可以转移所有权
功能：1.启动连接 2. 执行sql语句 3.isAlive()  4.reset（回滚未未完成事务，清除结果集合，） 5.获得错误信息 6. 最后活动时间

## ConnectionLease类
职责：从Database Pool中借用一个Connection连接，调用连接执行sql代码，在业务代码执行完毕，或者业务范围的错误时确保归还代码到连接池

1. 从Connection Pool中借用一个连接，类型为 Unique_ptr<DatabaseConnection>
2. 析构函数调用 releaseReturnedConnection函数，
``
   "header": {
   "fin": true,
   "opcode": 1,
   "opcode_name": "text",
   "payload_length": 100
   },
   "payload_format": "json",
   "payload": {
   "type": "chat.send",
   "request_id": "req-1001",
   "data": {
   "conversation_id": "C1",
   "content": "hello"
   }
   }
   }``