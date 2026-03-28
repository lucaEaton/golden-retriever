//
// Created by luca eaton on 3/28/26.
//

#include "Session.h"

Session& Session::get() {
    static Session instance;
    return instance;
}

bool Session::isExpired() const{
    return std::time(nullptr) >= expires_at;
}