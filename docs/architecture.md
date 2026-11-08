# 整体架构设计

## 2026-11-03：文件模块扩展方案（待实现）

头像沿用普通 HTTP → Handler → AvatarService → AvatarRepository，磁盘操作提交共用磁盘 I/O 池；不接入 file_uploads。聊天附件使用 WebSocket 控制许可、状态、续签与取消，独立 HTTP UploadReactor 接收密文，经有界队列写盘。完成后仍由设备信封传递加密描述，文件本体另走 HTTP 下载。维护线程挑选清理对象，磁盘池执行删除。

完整设计与 SQL 草案见 [file-storage](file-storage/README.md) 和 [附件协议](file-storage/attachment-protocol.md)。下方保留基础分层，新增 type/路由尚未实现。

## 基础请求分层

Client

↓

 +++++++system boundary++++++

TcpServer -> Connection -> HttpParser ->
Router

↓

Handler -> Service-> Repository->

PostgreSQL/redis
## TCP server
TcpServer负责建立并维护服务器接受TCP连接的入口，不负责客户端具体的传输数据，和业务核心逻辑的实现

- 创建 Socket `socket()`
- 绑定ip和端口到socket `bind()`
- 开始监听收到的TCP连接 `listen()`
- 接受client socket到下一层
- 管理生命周期

TcpServer不负责`accept()`之后的client连接，建立连接后byte会被交给下一层，不负责解析HTTP
## Connection
Connection 从TcpServer中接收client_fd，拥有并且管理其生命周期

- 读写原始字节byte `recv()` 和 `send()`
- 关闭连接
- 管理连接状态，例如超时

不负责解析HTTP等上层协议

## HTTP
从TCP中传递来的是原始字节，该层负责把网络传输层中的原始字节翻译成应用层理解的HTTP请求，反过来同理

Connection从recv中接受的是01字节，要将他翻译成HTTP请求的格式
不负责转发url到具体的service层；不负责理解json的业务含义

## handler
### Router
负责根据 HTTP 请求的请求方法（Method）和请求路径（Path）匹配对应的 Handler，并将请求分发给该 Handler 处理。
- 负责注册和维护路由规则
- 如果存在不匹配的情况，维护错误码

Router不负责HTTP的解析

### Handler 请求处理模块
负责处理某一类具体的 HTTP 请求，将 HttpRequest 转换为对应的应用层操作，并将应用层执行结果转换为 HttpResponse。
- 从 HttpRequest 中提取当前操作所需的输入数据。
- 执行请求层面的参数检查和格式验证。
- 调用对应的 Service 完成应用操作。
- 将 Service 返回的结果或错误转换为适当的 HttpResponse。


## Service
具体的业务逻辑
不负责直接访问数据库，通过repository访问

## repository
构造连接，访问数据库，访问结果返回service层



