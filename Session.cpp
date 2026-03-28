//
// Created by luca eaton on 3/28/26.
//

#include "Session.h"

#include <iostream>
#include <__ostream/basic_ostream.h>

Session& Session::get() {
    static Session instance;
    return instance;
}

bool Session::isExpired() const{
    const bool expired = std::time(nullptr) >= expires_at;
    if (expired) std::cout << "session expired, refreshing..." << std::endl;
    return expired;
}