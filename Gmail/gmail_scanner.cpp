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
#include <chrono>

#include "../Session.h"
#include "../turbo-b64/turbob64.h"
#include "cpp_email_classifier.h"

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    static_cast<std::string*>(userp)->append(static_cast<char *>(contents), size * nmemb);
    return size * nmemb;
}
inline bool isValidDate(const std::string& date) {
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
 * @param debug
 *      if set to true, all print statements used for debugging purposes will display
 *      automatically set to false if not specified
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
    // TODO: Theses subject statements only pull "Applied" emails, not necessarily "Rejection" or "Offer" emails.
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
        if (json meta = json::parse(meta_response, nullptr, false); !meta.is_discarded()) {
            if (debug) std::cout << meta << std::endl;
            metadata_emails_.push_back(std::move(meta));
        }
    }
    curl_easy_cleanup(curl);
}
std::string decode_B64(const std::string &in) {
    if (in.empty()) return "";
    auto s = in;
    std::ranges::replace(s, '-', '+');
    std::ranges::replace(s, '_', '/');
    std::erase_if(s, [](unsigned char c) { return std::isspace(c); });
    while (!s.empty() && s.back() == '=') s.pop_back();
    while (s.size() % 4 != 0) s += '=';
    if (s.empty()) return "";
    const size_t decoded_len = tb64declen(reinterpret_cast<const unsigned char*>(s.data()), s.size());
    std::string decoded(decoded_len + 16, '\0');
    const size_t actual_len = tb64dec(reinterpret_cast<const unsigned char*>(s.data()), s.size(), reinterpret_cast<unsigned char*>(decoded.data()));
    decoded.resize(actual_len);
    if (decoded.empty()) { std::cerr << "invalid base64 input or size = 0" << std::endl; return ""; }
    return decoded;
}

static std::string clean_html(const std::string& html) {
    std::string s = std::regex_replace(html, std::regex(R"(<style[^>]*>[\s\S]*?</style>)"), " ");
    s = std::regex_replace(s, std::regex("<[^>]+>"), " ");
    s = std::regex_replace(s, std::regex(R"(\s{2,})"), " ");
    return s;
}

static std::string extract_role(const std::string& body) {
    static const std::string kw =
     "(?:software|hardware|firmware|generative|backend|game|frontend|full.stack|"
     "ai|ml|data|security|systems|mobile|platform|application|automation)?"
     R"(\s*(?:engineer(?:ing)?|developer|intern(?:ship)?|researcher|scientist|analyst|architect|intern[-\s]))";

    static const std::vector<std::regex> patterns = {
        std::regex(R"(for (?:the|our|this)\s+((?:[\w,()/+\-'.]+ ){0,8}?)" + kw + R"((?:\s+[\w,()/+\-.]+){0,10}?)\s*(?:role|position|job)\b)", std::regex_constants::icase),
        std::regex(R"(position(?:\s+of|:)\s+((?:[\w,()/+\-'.]+ ){0,8}?)" + kw + R"((?:\s+[\w,()/+\-.]+){0,10}?)(?:\s*[–-]|\s*$|\.))", std::regex_constants::icase),
        std::regex(R"(applying to the\s+((?:[\w,()/+\-'.]+ ){0,8}?)" + kw + R"((?:\s+[\w,()/+\-.]+){0,10}?)\s*(?:role|position|job|$))", std::regex_constants::icase),
        std::regex(R"(application for\s+(?:(?:the|our|this)\s+)?((?:[\w,()/+\-'.]+ ){0,8}?)" + kw + R"((?:\s+[\w,()/+\-.]+){0,10}?)\s*(?:,|\.|;| and | has |\n|$))", std::regex_constants::icase),
        std::regex(R"(position\s*\(\s*\d+\s+((?:[\w,()/+\-'.]+ ){0,8}?)" + kw + R"((?:\s+[\w,()/+\-.]+){0,10}?)\s*\))", std::regex_constants::icase),
        std::regex(R"(interest in (?:(?:the|our|this)\s+)?((?:[\w,()/+\-'.]+ ){0,8}?)" + kw + R"((?:\s+[\w,()/+\-.]+){0,10}?)\s*(?:role|position|job)\b)", std::regex_constants::icase),
    };

    std::smatch m;
    for (auto& re : patterns) {
        if (std::regex_search(body, m, re)) {
            std::string role = m[1].str();
            role = std::regex_replace(role, std::regex(R"(^\s+|\s+$)"), "");
            if (!role.empty()) return role;
        }
    }
    //std::cerr << "failed search : " << body << std::endl;
    return "";
}

std::ostream operator<<(const std::ostream & lhs, const std::chrono::duration<long long, std::ratio<1, 1000>> & rhs);

/**
* @brief Fetches and processes full email content for each message retrieved by scan(),
*        extracting company name, application date, and classification status.
*
* Calls scan() to gather metadata_emails_, then iterates over each
* message to extract the Subject, From, and Date headers. The email body is decoded
* from base64 and passed to the Python ML classifier to determine application status.
*
* Company name extraction is attempted first via a regex match on the Subject header,
* looking for patterns like "your application to <Company>". If the subject match is
* empty, generic (contains "Intern" or "Engineer"), the company name is instead derived
* from the sender's email domain. Workday-hosted applications are handled as a special
* case, extracting the subdomain from the sender address instead.
*
* Three email body structures are handled:
*   - Simple single-part body (no "parts" key) — decoded directly from payload["body"]["data"]
*   - Multipart with text/plain part — decoded from parts["body"]["data"]
*   - Multipart/related with nested text/html or text/plain — decoded from inner parts
*
* @param date
*   The earliest date to scan from, in YYYY/MM/DD format (e.g. "2025/01/01").
*   Passed directly to scan() — see scan() for validation details.
*
* @param debug
*   If true, prints each extracted email's company, date, status, and thread ID
*   to stdout as it is processed, and prints the total email count at the end.
*   Defaults to false.
*
* @note Status is set to "Unknown" if the classifier returns an empty string,
*       which can occur when the email body is empty or fails to decode.
*
* @note The thread ID is converted from a hex string to a decimal string to
*       normalize it for storage and comparison.
*
* @note The role field in EmailMetadata is not currently populated and is
*       stored as an empty string.
*
* @see scan()
* @see classify()
* @see decode_B64()
*/
void gmail_scanner::fetch(const std::string& date, const bool debug) {
    scan(date,false);
    const auto t_start = std::chrono::high_resolution_clock::now();
    std::cout << "fetching emails 🐕\n" << std::endl;
    std::regex company_regex(R"((?:to\s+)([^!.,\n-]+?)(?:\s+-\s+|\s+(?:has|have|is|was|will|are)|[!.,]|$))");
    std::smatch match;
    for (auto& email : metadata_emails_) {
        std::string company, from, date_applied, status, role, subject;
        std::string thread_id = email.value("threadId", "");
        thread_id = std::to_string(std::stoull(thread_id, nullptr, 16));
        auto& payload = email["payload"];
        for (auto& header : payload["headers"]) {
            std::string name  = header.value("name", "");
            std::string value = header.value("value", "");
            if (name == "Subject") {
                if (std::regex_search(value,match,company_regex)) {
                    company = match[1].str();
                    subject = value;
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
        //best case, data is up for grabs
        if (!payload.contains("parts") && payload["body"].contains("data")) {
            if (std::string data = payload["body"]["data"]; !data.empty()) {
                std::string body = decode_B64(data);
                std::ranges::transform(body, body.begin(), ::tolower);
                status = classify(body);
                role = extract_role(body);
            }
        }
        // multipart body
        if (payload.contains("parts")) {
            for (auto& parts : payload["parts"]) {
                const std::string mimeType = parts.value("mimeType", "");
                if (mimeType == "text/plain" || mimeType == "text/html") {
                    if (!parts["body"].contains("data")) continue;
                    std::string body = decode_B64(parts["body"]["data"]);
                    if (body.empty() && debug) {
                        std::cout << "\n\n[status classification] possibly empty decode for mime: " << mimeType << "| thread id: " << thread_id << "\n\n";
                        continue;
                    }
                    std::ranges::transform(body, body.begin(), ::tolower);
                    status = classify(body);
                    if (body.find('<') != std::string::npos) body = clean_html(body);
                    role = extract_role(body);
                }
                if (mimeType == "multipart/related" || mimeType == "multipart/alternative") {
                    for (const auto& inner_parts : parts["parts"]) {
                        const auto innerMimeType = inner_parts.value("mimeType", "");
                        if (innerMimeType != "text/html" && innerMimeType != "text/plain") continue;
                        if (!inner_parts["body"].contains("data")) continue;
                        std::string body = decode_B64(inner_parts["body"]["data"]);
                        if (body.empty() && debug) {
                            std::cout << "\n[status classification] possibly empty decode for mime: " << mimeType << " | thread id: " << thread_id << "\n\n";
                            continue;
                        }
                        std::ranges::transform(body, body.begin(), ::tolower);
                        status = classify(body);
                        if (body.find('<') != std::string::npos) body = clean_html(body);
                        role = extract_role(body);
                    }
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
        if (status.empty()) status = "Unknown";
        if (debug) std::cout << "company: '" << company << "' date: '" << date_applied << "' status: '" << status << " | Role: " << role << "' Thread ID: "<< thread_id << std::endl;
        emails_.emplace_back(company, role, date_applied, status);
    }
    const auto t_end = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start);
     if (debug) std::cout << "total emails: " << emails_.size()<<" | Run Time:  " << duration << std::endl;
}

std::vector<gmail_scanner::EmailMetadata> gmail_scanner::getEmailData() {
    return emails_;
}