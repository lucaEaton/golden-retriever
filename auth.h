//
// Created by luca eaton on 3/27/26.
//

#ifndef AUTH_H
#define AUTH_H
#include <cstddef>

class auth {
    public:
        static std::size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp);
        static void authenticate();
};



#endif //AUTH_H
