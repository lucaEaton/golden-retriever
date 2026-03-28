//
// Created by luca eaton on 3/28/26.
//

#include "gmail_scanner.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <curl/curl.h>

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    static_cast<std::string*>(userp)->append(static_cast<char *>(contents), size * nmemb);
    return size * nmemb;
}
bool isValidDate(const std::string& date) {
    std::tm t = {};
    return strptime(date.c_str(), "%Y/%m/%d", &t) != nullptr;
}

void gmail_scanner::scan(std::string& date) {
    if (!isValidDate(date)) {std::cerr << "invalid date format, expected YYYY/MM/DD (e.g. 2025/01/01)" << std::endl; return;}
    using json = nlohmann::json;
    const std::string p = std::string(getenv("HOME")) + "/.config/golden-retriever/config.json";
    std::ifstream file(p);
    if (!file){std::cerr << "error accessing file: " << p << std::endl; return;}
    json tokens;
    file >> tokens;
    std::string access_token = tokens.value("access_token", "");
    if (access_token.empty()) {std::cerr << "curr access token needs to be refreshed" << std::endl; return;}
    file.close();

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
    "subject:\"your application to\""
    "after:"+date;

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
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {std::cerr << "curl error: " << curl_easy_strerror(res) << "\n"; return;}
    std::cout << listIDs << std::endl;
    for (json ids = json::parse(listIDs); auto& email : ids["messages"]) {
        std::string emailID = email.value("id","");
        std::string url = "https://www.googleapis.com/gmail/v1/users/me/messages/" + emailID + "?format=metadata&metadataHeaders=Subject&metadataHeaders=From&metadataHeaders=Date";
        curl = curl_easy_init();
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        std::string meta_response;

        std::string auth_header = "Authorization: Bearer " + access_token;
        headers = nullptr;
        headers = curl_slist_append(headers, auth_header.c_str());

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &meta_response);

        curl_easy_perform(curl);
        json meta = json::parse(meta_response);
        std::string subject, from, date;
        std::cout << meta_response << std::endl;
        CURLcode res2 = curl_easy_perform(curl);
        if (res2 != CURLE_OK) {std::cerr << "curl error: " << curl_easy_strerror(res2) << "\n"; return;}
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
}
