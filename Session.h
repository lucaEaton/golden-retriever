//
// Created by luca eaton on 3/28/26.
//

#ifndef SESSION_H
#define SESSION_H
#include <string>


class Session {
    public:
        std::string access_token;
        std::string refresh_token;
        std::string currentSpreadsheetID;
        long expires_at= 0;

        static Session &get();
        bool isExpired() const;

        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;
    private:
        Session() = default;

};



#endif //SESSION_H
