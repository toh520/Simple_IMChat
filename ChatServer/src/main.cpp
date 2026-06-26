#include "server/ChatServer.h"
#include "server/chatservice.hpp"
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    int port = 8888;
    std::string ip = "127.0.0.1";
    
    if (argc >= 2) {
        try {
            port = std::stoi(argv[1]);
        } catch (...) {
            std::cerr << "无效的端口参数，使用默认端口 8888" << std::endl;
        }
    }
    
    if (argc >= 3) {
        ip = argv[2];
    }

    try {
        // 创建服务器实例，监听 Port 端口
        ChatServer server(port);
        
        // 初始化业务服务端的物理节点信息并清理残留路由
        ChatService::instance()->setHostInfo(ip, port);
        
        // 启动服务循环
        server.start();

    } catch (const std::exception& e) {
        std::cerr << "服务器异常退出: " << e.what() << std::endl;
    }
    return 0;
}