# 服务端代码修改记录 - 第一阶段：应用层双向 ACK 与消息可靠投递机制

本阶段主要针对分布式即时通讯系统的消息“可靠投递”与“防止假死连接”问题，在 Linux 服务端进行底层与业务层的重构。

---

## 1. 解决的具体工程与设计问题
1. **网络假死与连接清理**：TCP 虽有 Keep-Alive，但检测周期过长且无法感知应用层死锁。当客户端网络异常断开但未触发 FIN 时，服务端套接字会处于“假死在线”状态。需要引入应用层心跳检测与业务层直接强切（`shutdown`）物理连接的接口。
2. **消息丢失隐患**：原有一对一单聊（`ONE_CHAT_MSG`）在转发时是单向“发送即忘”模型。一旦接收方网络产生抖动、丢包或异常下线，转发给接收方的包将在传输层丢失，而发送方和服务器均无法感知，导致消息彻底丢失。
3. **多端状态一致性**：引入重传后，如果用户在重传期间正常下线或被异常踢出，重传队列中的后续消息会继续尝试向已关闭的连接发送，这可能导致底层写出错或资源泄露。需要实现连接销毁时重传消息的自动清理并转存为离线消息。

---

## 2. 整体代码架构与设计思路
服务端可靠投递框架基于 **应用层双向确认 (Double ACK)** 设计：
* **发送端确认 (Send ACK)**：服务端接收到发送方 A 的 `ONE_CHAT_MSG` 报文后，立即回发 `MSG_SEND_ACK` 答复 A，告知服务器已成功“接管”该消息，A 即可停止本地的发送重传定时器。
* **接收端确认 (Recv ACK)**：
  - 服务端转发消息给 B 后，将该消息封装为 `PendingRecvMsg` 结构，注册到 `ChatService` 的 `_pendingRecvAckMap` 重传哈希表中，记录首发时间戳与 `retryCount=0`。
  - 启动独立的**重传扫描守护线程**，每 2 秒遍历一次该哈希表。对于超时 3 秒未收到 B 答复的消息，执行重传；重传满 3 次仍未收到确认的，将消息转存入 MySQL `OfflineMessage` 表，并主动剔除 B 的连接，强制收回套接字资源。
  - B 收到消息并回复 `MSG_RECV_ACK` 后，服务端按 `msg_id` 从哈希表中移除该记录。

---

## 3. 核心修改文件及函数变化点

### 3.1 协议定义层 ([msg.proto](file:///d:/code/backend/code/App/ChatServer/proto/msg.proto) & [public.hpp](file:///d:/code/backend/code/App/ChatServer/include/public.hpp))
* **协议扩充**：
  * 为 `OneChatRequest` 增加了唯一消息标识符 `int64 msg_id`（由客户端通过雪花算法生成）。
  * 新增 `MsgSendAck`（服务端答复发送端）和 `MsgRecvAck`（接收端答复服务端）的 Protobuf 消息定义。
* **消息类型映射**：
  * 在公共头文件 `public.hpp` 的 `EnMsgType` 枚举中，新增 `MSG_SEND_ACK` (7) 和 `MSG_RECV_ACK` (8) 的消息 ID。

### 3.2 底层网络层 ([TcpConnection](file:///d:/code/backend/code/App/ChatServer/src/net/TcpConnection.cpp))
* **新增物理断开接口 `TcpConnection::shutdown()`**：
  * 通过原子操作 `closed_.exchange(true)` 保证线程安全。
  * 将对应的文件描述符从 Linux Epoll 监听红黑树中移除 (`EPOLL_CTL_DEL`)。
  * 调用绑定的关闭回调 `closeCallback_` 通知业务层（`ChatServer`）彻底释放连接资源并析构 `TcpConnection`，底层 `Socket` 析构时会自动调用 `close(fd)` 关闭内核套接字。

### 3.3 业务逻辑层 ([ChatService](file:///d:/code/backend/code/App/ChatServer/src/server/chatservice.cpp))
* **生命周期与重传线程管理**：
  * 构造函数中初始化 `_running(true)` 并创建独立的守护线程执行 `retransmitLoop`。
  * 析构函数中设置 `_running(false)`，并优雅 join 守护线程。
  * 在消息处理器映射表中，注册 `MSG_RECV_ACK` 类型的回调映射到新方法 `recvAck` 上。
* **`oneChat` (单聊转发逻辑重构)**：
  * 在解析消息后，**立即**实例化并回发 `MsgSendAck` 报文给发送端 `conn`。
  * 转发给在线的接收方 B 后，实例化 `PendingRecvMsg` 存入 `_pendingRecvAckMap`。
* **`handleRedisSubscribeMessage` (跨服路由逻辑重构)**：
  * 跨服从 Redis 订阅通道读到消息并转发给本地的连接时，同样反序列化取出 `msg_id`，将消息纳入当前服务器实例的重传确认队列中，实现分布式环境下的重传可靠性。
* **`recvAck` (确认接收业务实现)**：
  * 接收端客户端发出消息到达的 `MSG_RECV_ACK` 后，解析出其中的 `msg_id` 并加锁将其从 `_pendingRecvAckMap` 中清除，中止重传逻辑。
* **`clientCloseException` (清理下线路由重构)**：
  * 当客户端正常/异常断开时，遍历 `_pendingRecvAckMap`。将所有发送至该用户的待确认消息移除，转存写入 MySQL `OfflineMessage` 离线消息表中，防止已断开连接的消息丢失，实现业务状态闭环。
* **`retransmitLoop` (重传扫描核心实现)**：
  * 独立线程每 2 秒唤醒一次，遍历 `_pendingRecvAckMap`。
  * 超时 3 秒未响应且 `retryCount < 3` 的消息，调用 `conn->send()` 重新触发发送，并更新状态。
  * `retryCount >= 3` 时，直接将消息转入 MySQL 离线表，调用 `clientCloseException(conn)` 清退状态，并调用 `conn->shutdown()` 主动关闭失效的长连接。
