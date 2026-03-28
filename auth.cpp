//
// Created by luca eaton on 3/27/26.
//

#include "auth.h"

#include <string>
#include <curl/curl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sys/stat.h>
/**
 * @note
 * Sourced from StackOverflow, will allow me to store the JSON data within a string, to further than parse through the data.
 * O(N) Space complexity.
 *
 * We could have this write to a JSON file allowing for O(1) space complexity
 * within the program and have it stored directly on the disk.
*
 * @param contents
 *   Pointer to the raw data buffer received from libcurl.
 *
 * @param size
 *   Size in bytes of a single data element.
 *
 * @param nmemb
 *   Number of data elements received in this callback invocation.
 *   The total number of bytes received is `size * nmemb`.
 *
 * @param userp
 *   User-provided pointer passed via CURLOPT_WRITEDATA.
 *   In this implementation it is expected to be a pointer to a
 *   std::string where the response data will be appended.
 *
 * @return
 *   The number of bytes successfully processed. libcurl expects
 *   this to equal `size * nmemb`. Returning a smaller value signals
 *   an error and aborts the transfer.
 */
size_t auth::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    static_cast<std::string*>(userp)->append(static_cast<char *>(contents), size * nmemb);
    return size * nmemb;
}
void auth::authenticate() {
    const std::string clientID = std::getenv("GOOGLE_CLIENT_ID");
    const std::string url =
    "https://accounts.google.com/o/oauth2/v2/auth?"
    "client_id=" + clientID +
    "&redirect_uri=http://127.0.0.1:8080" +
    "&response_type=code" +
    "&access_type=offline" +
    "&prompt=consent" +
    "&scope=https://www.googleapis.com/auth/gmail.readonly"
           "%20https://www.googleapis.com/auth/spreadsheets";

    system(("open \"" + url + "\"").c_str());
    // stores the file descriptor, AF_INET -> IPv4, SOCK_STREAM -> TCP Protocol, utilizing 0 is the default for TCP Protocol
    int sockFD = socket(AF_INET, SOCK_STREAM, 0);
    // basically saying setting up our rules on where we want to connect, IPv4, any ip, has to be on port 8080
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(8080);
    const int serverBind = bind(sockFD, reinterpret_cast<struct sockaddr *>(&serverAddr), sizeof(serverAddr));
    if (serverBind == -1) {std::cerr << "error within binding" << std::endl; return;}
    listen(sockFD, 1);
    const int newSockFD = accept(sockFD, nullptr, nullptr);
    char buffer[4096] = {}; // 4KB
    read(newSockFD, buffer, sizeof(buffer));
    std::cout << buffer << std::endl;
    const auto httpmsg = "HTTP/1.1 200 OK\r\nContent-Length: 33\r\n\r\nReturn back to the your terminal.";
    write(newSockFD, httpmsg, strlen(httpmsg));
    close(newSockFD);
    close(sockFD);
    const std::string request(buffer);
    const auto c = request.find("code=");
    if (c == std::string::npos) {std::cerr << "no code found" << std::endl; return;}
    const auto codeStart = c + 5;
    const auto codeEnd   = request.find_first_of("& ", codeStart);
    std::string code = request.substr(codeStart, codeEnd - codeStart);
    std::string clientSecret = std::getenv("GOOGLE_CLIENT_SEC");
    const std::string postBody =
        "code="          + code +
        "&client_id="    + clientID +
        "&client_secret=" + clientSecret +
        "&redirect_uri=http://127.0.0.1:8080" +
        "&grant_type=authorization_code";

    CURL *curl = curl_easy_init();
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, "https://oauth2.googleapis.com/token");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,postBody.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, auth::WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {std::cerr << "curl error: " << curl_easy_strerror(res) << "\n"; return;}
    nlohmann::json tokens = nlohmann::json::parse(response);
    // "HOME" is just /Users/YourMachinesUserName
    std::string configDir = std::string(getenv("HOME")) + "/.config/golden-retriever";
    std::filesystem::create_directories(configDir);
    std::string tokenPath = configDir + "/tokens.json";
    std::ofstream file(tokenPath);
    if (!file.is_open()) {std::cerr << "Error: Could not open file for writing\n"; return;}
    // save the time for when our sesh expries.
    auto expiresAt = std::time(nullptr) + tokens["expires_in"].get<int>();
    tokens["expires_at"] = expiresAt;
    file << tokens.dump(4);
    file.close();
    // make it only readable by current user
    chmod(tokenPath.c_str(), 0600);
    std::cout << "Tokens saved to " << tokenPath << "\n";
}


int main() {
    auth::authenticate();
    return 0;
}