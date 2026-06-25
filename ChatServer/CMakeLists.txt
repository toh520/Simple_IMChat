cmake_minimum_required(VERSION 3.10)
project(ChatService)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
include_directories(${CMAKE_SOURCE_DIR}/include)

# [新增] 1. 查找 Protobuf 库
find_package(Protobuf REQUIRED)

# [新增] 2. 包含 Protobuf 的头文件目录 (否则编译找不到 <google/protobuf/message.h>)
include_directories(${Protobuf_INCLUDE_DIRS})
# 同时也包含一下我们自己的 ./proto 目录，因为生成的 .pb.h 在那里
include_directories(${CMAKE_SOURCE_DIR}/proto)

# [修改] 3. 查找源文件
# 我们需要把刚刚生成的 msg.pb.cc 也加入编译列表
# 这里的 GLOB_RECURSE 会自动把 src/ 下的 .cpp 和 proto/ 下的 .cc 都找出来
file(GLOB_RECURSE SOURCES "src/*.cpp" "proto/*.cc")

add_executable(ChatServer ${SOURCES})

# [修改] 4. 链接库
# 此时 hiredis 库会被动态链接
target_link_libraries(ChatServer 
    Threads::Threads 
    ${Protobuf_LIBRARIES} 
    mysqlclient
    hiredis # <--- 新增 hiredis 库链接
)