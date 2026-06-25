# 归档计划：客户端（IMchat）功能完善

- **执行时间**：2026-06-24
- **状态**：已批准并执行中

---

## 计划详情

本项目旨在完善 Qt 客户端（IMchat）的聊天功能，填补目前客户端与服务端在“单聊”业务上的断层，并实现完整的 UI 交互与自动心跳活性检测。

> [!IMPORTANT]
> **开发协作规范提醒**
> 服务端代码（位于 `ChatServer` 目录）将只作为读取和参考，绝对不直接进行修改。本次修改全部集中在客户端（`IMchat`）代码库中。

### 拟方案与细节

#### 1. 客户端网络层扩展 (`ImClient`)
*   **发送单聊消息**：新增接口 `void sendOneChat(int toId, const QString &msg)`。在内部将 `msg` 封装为 `chat::OneChatRequest`，序列化后通过 `ONE_CHAT_MSG` 类型发出。
*   **接收单聊消息**：在 `handleMessage` 中解析 `ONE_CHAT_MSG` 并解析出 `chat::OneChatRequest`，发射信号 `void oneChatReceived(int fromId, int toId, const QString &msg)`。
*   **心跳活性维持**：在 `ImClient` 内引入 `QTimer`。连接建立成功（`onConnected`）后，每 15 秒自动向服务端发送 `HEART_BEAT_MSG` 空负载包；断开连接时停止定时器。

#### 2. 聊天界面重构 (`ChatWidget`)
设计一个经典的 IM 双栏窗口：
*   **左栏：会话管理**
    *   `QListWidget` 展示活跃的会话列表（显示对方的 UID/名字）。
    *   顶部提供“发起新聊天”按钮，点击后弹窗要求输入对方的数字 UID，验证通过后新建会话。
*   **右栏：聊天区域**
    *   顶部 Label 展示“当前与 [UID] 聊天”。
    *   中间 `QListWidget` 作为消息日志显示区，通过气泡/富文本显示历史聊天消息。
    *   底部 `QTextEdit` 或 `QLineEdit` 用于编写要发送的消息。
    *   右下角“发送”按钮，并支持快捷键（Enter 或 Ctrl+Enter）发送。
*   **状态与历史记录管理**：
    *   内存中维护 `QMap<int, QList<Message>> chatHistory_` 保存每个会话的聊天历史。
    *   当收到 `oneChatReceived` 信号时，将消息归入相应 UID 的历史记录。如果当前正是与该 UID 的聊天会话，实时追加至右侧消息框；如果不是，在左侧会话列表中提示未读状态。

---

## 拟定修改范围

### 客户端网络组件

*   **imclient.h**
    *   新增 `sendOneChat` 接口。
    *   新增 `oneChatReceived` 信号。
    *   引入 `QTimer heartbeatTimer_` 成员变量和相应槽函数。
*   **imclient.cpp**
    *   实现 `sendOneChat` 接口，构造并发送 `OneChatRequest`。
    *   在 `handleMessage` 中，增加 `ONE_CHAT_MSG` 的分支逻辑，解析 `OneChatRequest` 并激发信号。
    *   在构造函数中连接心跳定时器的信号槽，在 `onConnected` / `onDisconnected` 中处理定时器的启动/停止。

### 客户端 UI 组件

*   **chatwidget.ui**
    *   使用 Qt Designer 控件设计双栏布局。
    *   左侧放置 `QListWidget`（会话列表）和“添加会话”按钮。
    *   右侧放置 `QLabel`（聊天对象标题）、`QListWidget`（消息显示区）、`QTextEdit`（文本输入区）及 `QPushButton`（发送按钮）。
*   **chatwidget.h**
    *   定义消息结构体 `Message`（包含发送方、接收方、内容和时间）。
    *   定义成员变量 `myUid_` 保存当前用户自身的 UID。
    *   定义 `chatHistory_` 映射表用于内存管理历史数据。
    *   定义槽函数 `onOneChatReceived` 用以接收网络层分发的数据。
    *   声明界面各个交互操作的槽函数（如切换会话、添加会话、发送消息）。
*   **chatwidget.cpp**
    *   在构造函数中绑定 `ImClient::oneChatReceived` 信号。
    *   实现“添加会话”逻辑（使用 `QInputDialog` 输入目标用户 ID）。
    *   实现切换会话时重新填充消息显示区的逻辑。
    *   实现发送消息逻辑，清空输入框并向网络层提交。
*   **loginwidget.cpp**
    *   在成功登录后，将登录用户的真实 UID 传递给 `ChatWidget` 实例，以便在界面中显示以及用于判断消息的发送/接收方身份。
