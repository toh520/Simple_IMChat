# Simple_IMChat 软件体系结构大作业 - UML与架构图大全

本文件为您汇总了本系统《软件体系结构课程设计报告》和《汇报PPT》中所需的全部 **9 张系统设计、时序与物理网络拓扑图**。所有图均基于标准的 Markdown Mermaid 语法进行编写。

---

## 💡 Mermaid 架构图生成与截图指南

### 1. 在 VS Code 中一键预览与导出（推荐）
*   **第一步：安装插件**：在 VS Code 插件市场搜索并安装 `Markdown Preview Mermaid Support` 插件（或使用带有 Mermaid 渲染支持的 `Markdown All in One` 插件）。
*   **第二步：开启预览**：右键点击本 `.md` 文件，选择 **“打开侧边预览 (Open Preview to the Side)”**（快捷键 `Ctrl + K, V`），即可在右侧实时看到精美绘制好的 UML 图与时序图。
*   **第三步：保存为图片**：
    *   *方式 A（快捷截图）*：直接对右侧预览区进行高清截图，裁剪边缘后即可贴入 Word 报告或 PPT 占位框中。
    *   *方式 B（高清导出）*：如果您安装了 `Markdown Preview Enhanced` 插件，在预览区的 Mermaid 图上右键，选择 **“Save as PNG”** 或 **“Export as SVG”**，即可无损导出超清矢量大图。

### 2. 在线一键生成并导出为 PNG/SVG
如果您不想在本地配置插件，也可以直接将本文件中的 Mermaid 代码块（即 ` ```mermaid ` 到 ` ``` ` 之间的文本）复制并粘贴到 **Mermaid Live Editor (https://mermaid.live/)** 官方在线编辑器中。在左侧粘贴代码，右侧即可生成超精美图表，并提供一键下载 PNG 或 SVG 格式的功能。

---

## 📊 UML与系统架构图源码合集

### 1. 异构混合风格总架构拓扑图
*   **应用位置**：Word 报告第三章开头、PPT 幻灯片第 3 页。
*   **展示作用**：展示 C/S 风格、分层架构、事件 Reactor 以及 Redis 共享仓库的混合流转模型。

```mermaid
graph TD
    subgraph ClientLayer["客户端展现层 (Qt C++ Client)"]
        UI["UI 界面 (Login/Chat/Friend Windows)"]
        Logic["IMClient 业务控制层"]
        NetClient["QTcpSocket 网络传输层 (Protobuf)"]
        UI <--> Logic
        Logic <--> NetClient
    end

    subgraph GatewayLayer["负载均衡网关层 (Nginx Gateway)"]
        Nginx["Nginx L4 TCP 反向代理"]
    end

    subgraph ServiceLayer["业务处理服务集群 (C++ Epoll ChatServer)"]
        S1["ChatServer 节点 1"]
        S2["ChatServer 节点 2"]
    end

    subgraph RepositoryLayer["数据存储与共享中心 (Shared Repositories)"]
        Redis["Redis (共享路由表 / 状态网关 / 消息总线)"]
        MySQL["MySQL (Timeline 历史消息 / 账户与好友关系)"]
    end

    NetClient -->|TCP 连接请求| Nginx
    Nginx -->|轮询分流| S1
    Nginx -->|轮询分流| S2

    S1 <-->|跨服 Pub/Sub / 在线状态感知| Redis
    S2 <-->|跨服 Pub/Sub / 在线状态感知| Redis

    S1 -->|异步存盘队列 / 自愈连接池| MySQL
    S2 -->|异步存盘队列 / 自愈连接池| MySQL
```

---

### 2. 核心社交用例模型图 (场景视图)
*   **应用位置**：Word 报告第 4.1 节、PPT 幻灯片第 4 页左侧。
*   **展示作用**：呈现即时通讯大作业的核心场景用例图。

```mermaid
graph LR
    User["终端用户 (Actor)"]
    subgraph IMChatSystem["Simple_IMChat 即时通讯系统"]
        UC1("注册与安全登录")
        UC2("添加好友与在线感知")
        UC3("单对单即时聊天 (双向 ACK)")
        UC4("群组创建与群聊")
        UC5("离线消息增量拉取")
    end
    User --> UC1
    User --> UC2
    User --> UC3
    User --> UC4
    User --> UC5
```

---

### 3. 客户端核心类结构 UML 类图 (逻辑视图)
*   **应用位置**：Word 报告第 4.2.1 节、PPT 幻灯片第 4 页右侧。
*   **展示作用**：描述 Qt 客户端面向对象设计类图。

```mermaid
classDiagram
    class IMClient {
        -QTcpSocket* socket_
        -QMap<int, PendingMessage> pendingSendAckMap_
        -int currentMsgSeq_
        +connectToServer(host, port)
        +sendMsg(type, protobufData)
        +handleRead()
        +handleAck(msgSeq)
    }
    class ChatWindow {
        -IMClient* client_
        +onSendButtonClicked()
        +appendMessage(sender, text)
    }
    class FriendListWindow {
        -IMClient* client_
        +updateFriendStatus(uid, isOnline)
    }
    ChatWindow --> IMClient : 依赖发送消息
    FriendListWindow --> IMClient : 订阅状态变更
```

---

### 4. 服务端消息分发核心类结构 UML 类图 (逻辑视图)
*   **应用位置**：Word 报告第 4.2.2 节、与图 4.2 组合展示。
*   **展示作用**：呈现服务端基于回调映射的高内聚类图设计。

```mermaid
classDiagram
    class TcpServer {
        -int epollFd_
        -ThreadPool* threadPool_
        +startLoop()
        -handleNewConnection()
    }
    class ChatService {
        -static ChatService instance_
        -unordered_map<int, MsgHandler> _msgHandlerMap_
        -unordered_map<int, Connection*> _userConnMap_
        +getHandler(msgType) MsgHandler
        +sendToUser(userId, packet)
    }
    class ConnectionPool {
        -static ConnectionPool pool_
        -list<MYSQL*> connList_
        +getConnection() MYSQL*
        +releaseConnection(MYSQL*)
    }
    TcpServer --> ChatService : 派发网络数据包
    ChatService --> ConnectionPool : 数据落盘与校验
```

---

### 5. 应用层双向 ACK 与滑动去重窗口时序图 (过程视图)
*   **应用位置**：Word 报告第 4.3.1 节、PPT 幻灯片第 5 页。
*   **展示作用**：说明网络瞬断时系统在应用层进行防丢包、防重传冗余的时序交互流程。

```mermaid
sequenceDiagram
    autonumber
    participant C_Sender as "发送端 Qt 客户端"
    participant Server as "ChatServer 服务端"
    participant C_Receiver as "接收端 Qt 客户端"

    Note over C_Sender: 消息放入待确认队列<br>启动重传定时器(seq=1001)
    C_Sender ->> Server: 发送单聊消息 (Msg_Data, seq=1001)
    
    rect rgb(240, 248, 255)
        Note over Server: 检查去重窗口<br>判定为新消息
        Server -->> C_Sender: 回复应用层确认 (Msg_Ack, seq=1001)
        Note over C_Sender: 从 pendingSendAckMap_ 中移除<br>取消重传定时器
    end

    Note over Server: 路由寻址，确定接收端在线
    Server ->> C_Receiver: 投递消息 (Msg_Data, seq=5002)
    Note over C_Receiver: 滑动窗口幂等校验<br>写入本地数据库渲染
    C_Receiver -->> Server: 回复应用层确认 (Msg_Ack, seq=5002)
    Note over Server: 彻底清除该消息路由上下文
```

---

### 6. 分布式跨节点消息 Pub/Sub 路由转发时序图 (过程视图)
*   **应用位置**：Word 报告第 4.3.2 节、PPT 幻灯片第 6 页。
*   **展示作用**：说明当两个聊天用户登录在不同的物理服务器节点时，系统如何借助 Redis 消息总线实现路由中转。

```mermaid
sequenceDiagram
    autonumber
    participant A as "UserA (连接在 Server1)"
    participant S1 as "ChatServer 1"
    participant Redis as "Redis 路由/PubSub"
    participant S2 as "ChatServer 2"
    participant B as "UserB (连接在 Server2)"

    A ->> S1: 发送消息给 UserB
    Note over S1: 1. 查询本地连接表<br>发现 UserB 不在本节点
    S1 ->> Redis: 2. 查询 HGET user:route "UserB"
    Redis -->> S1: 返回 "Server2" IP 与端口
    
    rect rgb(255, 245, 238)
        Note over S1: 3. 确定 UserB 登录在 Server2
        S1 ->> Redis: 4. PUBLISH "chat_server_channel_2" (MsgData)
        Note over S2: 5. Server2 启动时已订阅<br>"chat_server_channel_2"
        Redis -->> S2: 6. 接收订阅消息 (MsgData)
    end
    
    Note over S2: 7. 查询本地连接表<br>找到 UserB 的 Socket 描述符
    S2 ->> B: 8. 通过 TCP 发送数据包给 UserB
```

---

### 7. 服务端 Epoll Reactor 与异步批量刷盘多线程协作图 (过程视图)
*   **应用位置**：Word 报告第 4.3.3 节。
*   **展示作用**：描述系统多线程并发架构，说明网络主线程、业务工作线程池与异步写盘线程的协同。

```mermaid
sequenceDiagram
    autonumber
    actor User as "客户端连接"
    participant Epoll as "Epoll Reactor 主线程"
    participant TP as "Worker 业务线程池"
    participant Redis as "Redis 缓存队列"
    participant DBThread as "MySQL 异步写盘线程"
    participant DB as "MySQL 物理库"

    User ->> Epoll: 1. 触发读就绪事件 (EPOLLIN)
    Epoll ->> TP: 2. 读取套接字数据包并分发任务
    Note over TP: 3. 解析包，校验业务逻辑<br>将消息放入内存待存盘队列
    TP ->> Redis: 4. 写入 ZSet 维持热点 100 条缓存
    TP -->> User: 5. 立即回复应用层响应（实现高吞吐）
    
    rect rgb(245, 255, 250)
        Note over DBThread: 6. 定时从缓存队列/内存中<br>批量 Pop 待落盘消息
        DBThread ->> DB: 7. 批量 INSERT INTO timeline_history
        DB -->> DBThread: 8. 落盘成功
    end
```

---

### 8. 基于 Docker Compose 的多容器物理部署拓扑图 (物理视图)
*   **应用位置**：Word 报告第 4.5 节、PPT 幻灯片第 7 页。
*   **展示作用**：展示分布式 IM 集群环境下一键部署的网络拓扑与映射端口。

```mermaid
graph TD
    Internet["外部公网访问 (客户端 TCP)"] -->|端口: 8000| NginxHost["物理机宿主机 (外网 IP)"]
    
    subgraph DockerBridge["Docker 内部桥接网络 (172.18.0.0/16)"]
        Nginx["Nginx 容器 (Port 8000)"]
        S1["ChatServer 1 容器 (Port 8888)"]
        S2["ChatServer 2 容器 (Port 8889)"]
        Redis["Redis 容器 (Port 6739 -> 映射宿主机 6379)"]
        MySQL["MySQL 容器 (Port 3306 -> 映射宿主机 3307)"]
    end
    
    NginxHost --> Nginx
    Nginx -->|TCP 负载均衡轮询| S1
    Nginx -->|TCP 负载均衡轮询| S2
    
    S1 <--> Redis
    S2 <--> Redis
    S1 <--> MySQL
    S2 <--> MySQL
```

---

### 9. 体系结构质量属性分级效用树 (Utility Tree 视图)
*   **应用位置**：Word 报告第 5.1 节、PPT 幻灯片第 8 页。
*   **展示作用**：以结构化图表形式呈现内部质量指标（开发态）与外部质量指标（运行态）的评估结构。

```mermaid
graph TD
    Root["质量效用树 (Utility Tree)"] --> Ext["外部质量 (运行态指标)"]
    Root --> Int["内部质量 (开发态指标)"]
    
    Ext --> Ext1["可用性 (Availability)"]
    Ext --> Ext2["性能 (Performance)"]
    Ext --> Ext3["安全性 (Security)"]
    
    Int --> Int1["可重用性 (Reusability)"]
    Int --> Int2["可移植性 (Portability)"]
    Int --> Int3["可测试性 (Testability)"]
    
    Ext1 --> Sc1["网络瞬断容错 (情景 1)"]
    Ext1 --> Sc2["数据库连接池自愈 (情景 2)"]
    Ext2 --> Sc3["高并发异步批量存盘 (情景 3)"]
    Ext2 --> Sc4["Timeline 离线消息增量拉取 (情景 4)"]
    Ext3 --> Sc5["恶意数据包 Session Token 鉴权 (情景 5)"]
    
    Int1 --> Sc6["Google Protobuf 通信协议共享 (情景 6)"]
    Int2 --> Sc7["Qt Network 跨平台接口封装 (情景 7)"]
    Int3 --> Sc8["数据访问层 DAL 抽象 Mock 单元测试 (情景 8)"]
```

---

### 10. 开发视图模块编译依赖图 (开发视图)
*   **应用位置**：Word 报告第 4.4 节。
*   **展示作用**：描绘客户端 UI 层、网络层与服务端业务层、实体层、DAL数据库连接池层的静态物理依赖及编译约束关系。

```mermaid
graph TD
    subgraph ClientDev["客户端静态编译依赖 (IMchat)"]
        UI_Dev["展现层 (UI模块)<br>mainwindow.h/cpp"]
        Net_Dev["网络层 (net模块)<br>imclient.h/cpp"]
        Qt_Dev["Qt6 Network 依赖框架"]
        UI_Dev -->|依赖调用| Net_Dev
        Net_Dev -->|继承与使用| Qt_Dev
    end

    subgraph CommonProto["共享通信协议层 (proto)"]
        Proto_Dev["Protobuf 协议定义<br>IMChat.proto"]
    end

    subgraph ServerDev["服务端静态编译依赖 (ChatServer)"]
        Main_Dev["启动层<br>main.cpp"]
        Service_Dev["业务路由层<br>chatservice.cpp"]
        Model_Dev["实体模型层<br>model/*"]
        DB_Dev["基础存储层<br>db/* (连接池)"]
        Epoll_Dev["网络驱动层<br>server/* (Epoll)"]
        
        Main_Dev -->|静态依赖| Service_Dev
        Main_Dev -->|静态依赖| Epoll_Dev
        Service_Dev -->|静态依赖| Model_Dev
        Service_Dev -->|静态依赖| DB_Dev
        Model_Dev -->|静态依赖| DB_Dev
    end
    
    subgraph ExtLibs["外部静态/动态库依赖"]
        ProtoLib["Protobuf 静态库"]
        MySqlLib["MySQL Connector"]
        HiRedisLib["Hiredis 驱动"]
    end

    %% 跨子系统依赖
    Net_Dev -.->|反序列化协议依赖| Proto_Dev
    Service_Dev -.->|序列化协议依赖| Proto_Dev
    
    %% 外部库链接
    Qt_Dev -->|静态链接| ProtoLib
    Epoll_Dev -->|静态链接| ProtoLib
    DB_Dev -->|静态链接| MySqlLib
    DB_Dev -->|静态链接| HiRedisLib
```

