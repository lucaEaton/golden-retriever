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
#include "../Gmail/gmail_scanner.h"

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    static_cast<std::string*>(userp)->append(static_cast<char *>(contents), size * nmemb);
    return size * nmemb;
}

void Spreadsheet::writeHeader() {
    const std::string spreadsheet_id = Session::get().currentSpreadsheetID;
    const std::string access_token = Session::get().access_token;

    CURL* curl = curl_easy_init();
    const std::string range = "Sheet1!A1:D1";
    char* encoded = curl_easy_escape(curl, range.c_str(), range.size());
    const std::string url = "https://sheets.googleapis.com/v4/spreadsheets/"
                    + spreadsheet_id
                    + "/values/" + std::string(encoded)
                    + "?valueInputOption=RAW";
    curl_free(encoded);

    using json = nlohmann::json;
    json body;
    body["values"] = json::array({ {"Company", "Role", "Date Applied", "Status"} });
    const std::string body_str = body.dump();

    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + access_token).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}

// TODO: it makes a new spreadsheet sometimes when one is alreadly created, I'm not sure why but its a bunch that needs to be fixed
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
    writeHeader();
}

void Spreadsheet::build_map() {
    email_map_.clear();
    const auto spreadsheet_id = Session::get().currentSpreadsheetID;
    const auto access_token = Session::get().access_token;
    const std::string url = "https://sheets.googleapis.com/v4/spreadsheets/"
                + spreadsheet_id
                + "/values:batchGet?ranges=Sheet1%21A1%3AD200";

    CURL* curl = curl_easy_init();
    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + access_token).c_str());

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    std::cout << response << "from build_map()" << std::endl;

    using json = nlohmann::json;
    json payload = json::parse(response);
    auto& values = payload["valueRanges"][0]["values"];
    if (values.size() <= 1) {
        return;
    }
    for (size_t i = 1; i < values.size(); i++) {  // start at 1 to skip header
        auto& row = values[i];
        email_map_[row[0].get<std::string>() + " " + row[1].get<std::string>()] = "";
    }

    std::cout << email_map_.size() << std::endl;
}

void::Spreadsheet::writeTo(const std::vector<gmail_scanner::EmailMetadata>& emails) {
    build_map();
    gmail_scanner g;
    std::vector<gmail_scanner::EmailMetadata> result;
    for (const auto& email : emails) {
        if (std::string key = email.company + " " + email.role; !email_map_.contains(key)) {
                result.push_back(email);
        }
    }

    std::cout << result.size() << std::endl;

    CURL* curl = curl_easy_init();
    std::string access_token = Session::get().access_token;
    const std::string spreadsheet_id = Session::get().currentSpreadsheetID;
    std::cout << spreadsheet_id << std::endl;
    std::string range = "Sheet1!A:D";
    char* encoded = curl_easy_escape(curl, range.c_str(), range.size());
    std::string url = "https://sheets.googleapis.com/v4/spreadsheets/"
                    + spreadsheet_id
                    + "/values/" + std::string(encoded)
                    + ":append?valueInputOption=RAW";
    curl_free(encoded);

    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + access_token).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    using json = nlohmann::json;
    json body;
    body["values"] = json::array();
    for (auto& e : result) {
        body["values"].push_back({e.company, e.role, e.date_applied, e.status});
    }

    std::string body_str = body.dump();

    std::string res;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res);

    CURLcode r = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (r != CURLE_OK) {
        std::cerr << "sheets append error: " << curl_easy_strerror(r) << std::endl;
        return;
    }

    std::cout << "wrote to sheet" << std::endl;
    std::cout << res << std::endl;
}