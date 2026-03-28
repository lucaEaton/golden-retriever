//
// Created by luca eaton on 3/27/26.
//

#ifndef AUTH_H
#define AUTH_H
#include <string>

class auth {
    public:
        static void authenticate();
        static void refresh(const std::string &tR);
};



#endif //AUTH_H
