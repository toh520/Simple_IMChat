#include "server/ChatServer.h"
#include <iostream>

int main() {
    try {
        // 创建服务器实例，监听 8888 端口
        ChatServer server(8888);
        
        // 启动服务循环
        server.start();

    } catch (const std::exception& e) {
        std::cerr << "服务器异常退出: " << e.what() << std::endl;
    }
    return 0;
}