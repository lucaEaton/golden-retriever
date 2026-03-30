//
// Created by luca eaton on 3/28/26.
//

#include "Spreadsheet.h"

#include <iostream>
#include <string>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "../Session.h"
#include "../Authentication/auth.h"

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    static_cast<std::string*>(userp)->append(static_cast<char *>(contents), size * nmemb);
    return size * nmemb;
}

void Spreadsheet::createSpreadsheet() {
    if (!Session::get().currentSpreadsheetID.empty()) {std::cerr << "Spreadsheet is already set | ID: " << Session::get().currentSpreadsheetID <<std::endl; return;}
    using json = nlohmann::json;
    json body;
    body["properties"]["title"] = "Golden Retriever Jobs";
    body["sheets"] = json::array({
        { {"properties", { {"title", "Sheet1"} }} }
    });
    const std::string body_str = body.dump();

    const std::string header_str = "Authorization: Bearer " + Session::get().access_token;
    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, header_str.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_URL, "https://sheets.googleapis.com/v4/spreadsheets");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());

    std::string response;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    const CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {std::cout << "curl error: " << curl_easy_strerror(res) << std::endl; return;}
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    json result = json::parse(response, nullptr, false);
    if (result.is_discarded() || !result.contains("spreadsheetId")) {
        std::cerr << "failed to create spreadsheet: " << response << "\n";
        return;
    }

    Session::get().currentSpreadsheetID= result["spreadsheetId"];
    auth::addToConfig("spreadsheetId", result["spreadsheetId"]);
    std::cout << "Created A New Spreadsheet: " << body["properties"]["title"] << " | ID: " << Session::get().currentSpreadsheetID << std::endl;
}
