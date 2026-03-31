//
// Created by luca eaton on 3/28/26.
//

#include "gmail_scanner.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <curl/curl.h>
#include <regex>
#include <sstream>
#include <unordered_set>

#include "../Session.h"
#include "../turbo-b64/turbob64.h"
#include "../turbo-b64/turbob64_.h"

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    static_cast<std::string*>(userp)->append(static_cast<char *>(contents), size * nmemb);
    return size * nmemb;
}
bool isValidDate(const std::string& date) {
    std::tm t = {};
    return strptime(date.c_str(), "%Y/%m/%d", &t) != nullptr;
}

/**
 * @brief Scans the authenticated user's Gmail inbox for job application
 *        confirmation emails and extracts metadata from each result.
 *
 * Queries the Gmail API using a broad set of subject-line keywords commonly
 * used by applicant tracking systems (e.g. "thanks for applying", "application
 * received"). Results are filtered by the provided date, limiting the search
 * to emails received after that point. For each matching message ID returned
 * by the list endpoint, a second request is made to fetch the Subject, From,
 * and Date headers via the metadata format.
 *
 * @param date
 *   The earliest date to scan from, in YYYY/MM/DD format (e.g. "2025/01/01").
 *   Validated before any API call is made — the function returns early on
 *   an invalid format.
 *
 * @note Requires a valid, non-expired access token to be present in Session::get().
 *       Call auth::refresh() before invoking this function if the session may
 *       be expired.
 *
 * @note Gmail's messages.list endpoint returns a maximum of 200 results per
 *       request. If the user has applied to more than 200 jobs since @p date,
 *       pagination via nextPageToken will be required.
 *
 * @note Each message ID from the list response incurs a separate HTTP request
 *       to the messages.get endpoint. For 200 results this means 201 total
 *       API calls — one list + one metadata fetch per message.
 *
 * @warning json::parse will throw if the API returns malformed or unexpected
 *          JSON (e.g. on a 401 Unauthorized response). Consider wrapping parse
 *          calls with the non-throwing overload:
 *          json::parse(str, nullptr, false) and checking is_discarded().
 *
 */
void gmail_scanner::scan(const std::string& date, const bool debug) {
    if (!isValidDate(date)) {std::cerr << "invalid date format, expected YYYY/MM/DD (e.g. 2025/01/01)" << std::endl; return;}
    std::string access_token = Session::get().access_token;
    if (access_token.empty()) {std::cerr << "curr access token needs to be refreshed" << std::endl; return;}
    std::string q =
        "subject:\"thanks for applying\" OR "
        "subject:\"thank you for applying\" OR "
        "subject:\"application received\" OR "
        "subject:\"we received your application\" OR "
        "subject:\"thanks for your application\" OR "
        "subject:\"thank you for your application\" OR "
        "subject:\"application confirmation\" OR "
        "subject:\"application submitted\" OR "
        "subject:\"you applied to\" OR "
        "subject:\"your application to\" "
        "after:" + date;

    CURL *curl = curl_easy_init();

    char* encoded = curl_easy_escape(curl, q.c_str(), q.size());
    std::string list_url = "https://www.googleapis.com/gmail/v1/users/me/messages?maxResults=200&q=" + std::string(encoded);
    curl_free(encoded); // delete

    std::string list_header = "Authorization: Bearer " + access_token;
    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, list_header.c_str());

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_URL, list_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);

    std::string listIDs;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &listIDs);
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        std::cerr << "curl error: " << curl_easy_strerror(res) << std::endl;
        curl_easy_cleanup(curl);
        return;
    }

    using json = nlohmann::json;
    for (json ids = json::parse(listIDs); auto& email : ids["messages"]) {
        std::string emailID = email.value("id","");
        std::string url = "https://www.googleapis.com/gmail/v1/users/me/messages/" + emailID + "?format=full";

        curl_easy_reset(curl);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        std::string auth_header = "Authorization: Bearer " + access_token;
        headers = nullptr;
        headers = curl_slist_append(headers, auth_header.c_str());

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);

        std::string meta_response;
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &meta_response);


        CURLcode res2 = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        if (res2 != CURLE_OK) {std::cerr << "curl error: " << curl_easy_strerror(res2) <<std::endl; continue;}
        json meta = json::parse(meta_response, nullptr, false);
        if (!meta.is_discarded()) {
            if (debug) std::cout << meta << std::endl;
            metadata_emails_.push_back(std::move(meta));
        }
    }
    curl_easy_cleanup(curl);
}
//since gmail uses safe b64, I assume turbo uses the standard b64 formatting, so we do this in order to avoid mismatched formats
std::string gTsB64(const std::string& in) {
    auto out = in;
    std::ranges::replace(out, '-', '+');
    std::ranges::replace(out, '_', '/');

    // add back missing padding
    while (out.size() % 4 != 0) {
        out += '=';
    }
    return out;
}

std::string decode_B64(const std::string &in) {
    const auto s = gTsB64(in);
    std::string decoded;
    decoded.resize(tb64declen(reinterpret_cast<const unsigned char *>(s.data()),s.size()));
    const auto body_size = tb64dec(reinterpret_cast<const unsigned char *>(s.data()), s.size(),reinterpret_cast<unsigned char *>(decoded.data()));
    if (body_size <= 0) {
        std::cerr << "invalid base64 input or input length = 0" << std::endl; return "";
    }
    decoded.resize(body_size);
    return decoded;
}

void gmail_scanner::fetch(const std::string& date, const bool debug) {
    scan(date,true);
    std::regex r(R"((?:to\s+)([^!.,\n-]+?)(?:\s+-\s+|\s+(?:has|have|is|was|will|are)|[!.,]|$))");
    std::smatch match;
    static const std::vector<std::pair<std::string, std::string>> status_keywords = {
        {"unfortunately","Rejected"},
        {"not moving forward","Rejected"},
        {"other candidates","Rejected"},
        {"position has been filled","Rejected"},
        {"decided to pursue","Rejected"},
        {"pleased to offer","Offer"},
        {"join our team","Offer"},
        {"offer","Offer"},
        {"interview","Interview"},
        {"next steps","Interview"},
        {"schedule a call","Interview"},
        {"coding challenge","Interview"},
        {"technical screen","Interview"},
        {"take home","Interview"},
        {"under review","In Review"},
        {"being reviewed","In Review"},
        {"not to proceed","Rejected"}
    };

    for (auto& email : metadata_emails_) {
        std::string company, from, date_applied, status;
        std::string thread_id = email.value("threadId", "");
        auto& payload = email["payload"];
        for (auto& header : payload["headers"]) {
            std::string name  = header.value("name", "");
            std::string value = header.value("value", "");
            if (name == "Subject") {
                if (std::regex_search(value,match,r)) {
                    company = match[1].str();
                }
            }
            else if (name == "Date") {
                std::tm tm = {};
                std::stringstream ss(value);
                ss >> std::get_time(&tm, "%a, %d %b %Y");
                char formatted[20];
                std::strftime(formatted, sizeof(formatted), "%Y/%m/%d", &tm);
                date_applied = std::string(formatted);
            }
            else if (name == "From") {from = value;}
        }
        for (auto& parts : payload["parts"]) {
            const std::string mimeType = parts.value("mimeType", "");
            if (mimeType == "text/plain") {
                if (!parts["body"].contains("data")) continue;
                std::string body = decode_B64(parts["body"]["data"]);
                status = "Applied";
                for (const auto& [k, s] : status_keywords) {
                    if (body.find(k) != std::string::npos) {
                        status = s;
                        break;
                    }
                }
                break;
            }
            if (mimeType == "multipart/related") {
                for (const auto& inner_parts : parts["parts"]) {
                    if (std::string innerMime = inner_parts.value("mimeType", ""); innerMime != "text/html" && innerMime != "text/plain" && !inner_parts["body"].contains("data")) continue; // ← add this
                    std::string body = decode_B64(inner_parts["body"]["data"]);
                    status = "Applied";
                    for (const auto& [k, s] : status_keywords) {
                        if (body.find(k) != std::string::npos) {
                            status = s;
                            break;
                        }
                    }
                    break;
                }
            }
        }

        if ((company.empty() || company.find("Intern") != std::string::npos || company.find("Engineer") != std::string::npos) ){
            if (auto it = from.find('@'); it != std::string::npos) {
                auto period = from.find('.', it);
                company = from.substr(it+1, period-it-1);
                if (company == "myworkday") {
                    auto it2 = from.find('<');
                    company = from.substr(it2+1, it-it2-1);
                }
                company[0] = std::toupper(company[0]);
            }
        }
        if (debug) std::cout << "company: '" << company << "' date: '" << date_applied << "' status: '" << status << "' Thread ID: "<< thread_id << std::endl;
        emails_.emplace_back(company, "", date_applied, status);
    }
}

std::vector<gmail_scanner::EmailMetadata> gmail_scanner::getEmailData() {
    return emails_;
}