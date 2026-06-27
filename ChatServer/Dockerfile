# 阶段 1: 编译构建
FROM ubuntu:22.04 AS builder

# 安装编译所需的开发环境及第三方库
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential \
    cmake \
    libprotobuf-dev \
    protobuf-compiler \
    libmysqlclient-dev \
    libhiredis-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# 拷贝服务端代码
COPY . .

# 编译生成可执行文件
RUN mkdir build && cd build && cmake .. && make

# 阶段 2: 运行时环境
FROM ubuntu:22.04

# 安装运行时依赖的共享链接库
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    libprotobuf-dev \
    libmysqlclient-dev \
    libhiredis-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 从构建阶段拷贝二进制程序
COPY --from=builder /build/bin/ChatServer .

# 拷贝初始的 MySQL 配置文件
COPY mysql.conf .

# 默认容器暴露服务端口
EXPOSE 8888

# 启动入口：可通过 CMD 传入具体监听的 Port 和 本机 IP/容器名
ENTRYPOINT ["/app/ChatServer"]
CMD ["8888", "127.0.0.1"]
