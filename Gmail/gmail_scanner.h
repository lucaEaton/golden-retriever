//
// Created by luca eaton on 3/28/26.
//

#ifndef GMAIL_SCANNER_H
#define GMAIL_SCANNER_H
#include <string>
#include <nlohmann/json.hpp>
#include <utility>

class gmail_scanner {
    public:
        struct EmailMetadata {
            std::string company;
            std::string role;
            std::string date_applied;
            std::string status;
            EmailMetadata(std::string company, std::string role,
                          std::string date, std::string status)
                : company(std::move(company)), role(std::move(role)),
                  date_applied(std::move(date)), status(std::move(status)) {}
        };
        void scan(const std::string &date, bool debug = false);
        void fetch(const std::string &date, bool debug = false);
        std::vector<EmailMetadata> getEmailData();
    private:
        std::vector<nlohmann::json> metadata_emails_;
        std::vector<EmailMetadata> emails_;
};

#endif //GMAIL_SCANNER_H