//
// Created by luca eaton on 3/27/26.
//

#ifndef AUTH_H
#define AUTH_H
#include <string>

class auth {
    public:
        static void authenticate();
        static void refresh();
        static void loadSession();
        static void addToConfig(const std::string &key, const std::string &value);
};

#endif //AUTH_H
