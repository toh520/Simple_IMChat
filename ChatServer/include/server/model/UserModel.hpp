#pragma once
#include "server/model/User.hpp"

class UserModel {
public:
    // User表的增加方法（注册）
    bool insert(User& user);

    // 根据用户ID查询用户信息（登录验证）
    User query(int id);

    // 更新用户的状态信息
    bool updateState(User user);

    // 重置所有用户的状态信息（服务器重启时使用）
    void resetState();
};