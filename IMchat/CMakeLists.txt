cmake_minimum_required(VERSION 3.16)

project(IMchat VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 新增：MSVC UTF-8 编码配置===============
if(MSVC)
    add_compile_options(/utf-8)
endif()
#=======================================

# 【修复处】protobuf
list(APPEND CMAKE_PREFIX_PATH "D:/tools/vcpkg/vcpkg/installed/x64-windows")

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)
find_package(Protobuf CONFIG REQUIRED)

# 新增：头文件搜索路径=========================================
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/ui
    ${CMAKE_SOURCE_DIR}/net
    ${CMAKE_SOURCE_DIR}/proto
    ${Protobuf_INCLUDE_DIRS}
)

# 新增：收集 ui 目录源文件 (注意 GLOB 最好不要包含 .ui，.ui 单独加比较稳妥)
file(GLOB UI_SOURCES
    "${CMAKE_SOURCE_DIR}/ui/*.h"
    "${CMAKE_SOURCE_DIR}/ui/*.cpp"
    "${CMAKE_SOURCE_DIR}/ui/*.ui"
)

file(GLOB NET_SOURCES
    "${CMAKE_SOURCE_DIR}/net/*.h"
    "${CMAKE_SOURCE_DIR}/net/*.cpp"
)
#=============================================================

set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    ${UI_SOURCES}
    ${NET_SOURCES}
    proto/msg.pb.cc
    proto/public.h   # 【修复处】把 public.h 放在这里，删掉后面的 target_sources
)

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/proto/msg.pb.h" OR NOT EXISTS "${CMAKE_SOURCE_DIR}/proto/msg.pb.cc")
    message(FATAL_ERROR "缺少 proto/msg.pb.h 或 proto/msg.pb.cc。请先在 IMchat/proto 目录执行 protoc 生成 C++ 文件。")
endif()

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    # 【修复处】：直接使用 ${PROJECT_SOURCES} 即可，去掉了下面重复的 loginwidget 等文件
    qt_add_executable(IMchat
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    if(ANDROID)
        add_library(IMchat SHARED ${PROJECT_SOURCES})
    else()
        add_executable(IMchat ${PROJECT_SOURCES})
    endif()
endif()

# 【修复处】：Protobuf 官方 vcpkg 导出的目标通常是 protobuf::libprotobuf (小写)，有时候大写会报错，建议用小写
target_link_libraries(IMchat PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    protobuf::libprotobuf
)

if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.IMchat)
endif()

set_target_properties(IMchat PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS IMchat
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(IMchat)
endif()
