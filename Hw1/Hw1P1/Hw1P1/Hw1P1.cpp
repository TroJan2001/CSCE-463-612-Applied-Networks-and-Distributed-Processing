//Semester: Spring/2025
//Course: CSCE 612
//Hw1P1

#include <iostream>
#include <string>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ctime>
#include "HTMLParserBase.h"

#pragma comment(lib, "Ws2_32.lib")

using namespace std;

struct URLParts {
    string scheme;
    string host;
    int port = 80;  // Default to HTTP port 80
    string path = "/";   // Default to root path
    string query;
    string fragment;
};

void printUsage() {
    cout << "Usage: Hw1P1.exe [URL]" << endl;
}

bool isIPAddress(const string& host) {
    struct sockaddr_in sa;
    return inet_pton(AF_INET, host.c_str(), &(sa.sin_addr)) == 1;

}

bool resolveDNS(const string& host, const string& service, struct addrinfo*& result) {
    clock_t start = clock();
    cout << "\tDoing DNS... ";

    struct addrinfo addrInfo = {};
    addrInfo.ai_family = AF_INET;
    addrInfo.ai_socktype = SOCK_STREAM;
    addrInfo.ai_protocol = IPPROTO_TCP;

    int res = getaddrinfo(host.c_str(), service.c_str(), &addrInfo, &result);
    clock_t end = clock();
    double duration = (double)(end - start) / CLOCKS_PER_SEC * 1000.0;

    if (res != 0 || result == nullptr) {
        cout << "failed with " << WSAGetLastError();
        return false;
    }

    struct sockaddr_in* ipv4 = (struct sockaddr_in*)result->ai_addr;
    char ipstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(ipv4->sin_addr), ipstr, sizeof(ipstr));
    cout << "done in " << duration << " ms, found " << ipstr << endl;

    return true;
}

SOCKET createSocket() {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        cout << "Socket creation failed with error " << WSAGetLastError() << endl;
        return INVALID_SOCKET;
    }
    return sock;
}

bool connectToServer(SOCKET sock, struct addrinfo* result) {
    clock_t start = clock();
    cout << "\t* Connecting on page...  ";
    for (struct addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
        int connectStatus = connect(sock, ptr->ai_addr, ptr->ai_addrlen);
        clock_t end = clock();
        double elapsed = static_cast<double>(end - start) / CLOCKS_PER_SEC * 1000;

        if (connectStatus == 0) {
            cout << "done in " << elapsed << "ms" << endl;
            return true;
        }

        cout << "failed with " << WSAGetLastError() << endl;
    }
    return false;
}

bool sendRequest(SOCKET sock, const string& request) {
    if (send(sock, request.c_str(), request.length(), 0) == SOCKET_ERROR) {
        cout << "Failed with " << WSAGetLastError() << endl;
        return false;
    }
    return true;
}


//Use vector for dynamic memory management
bool receiveResponse(SOCKET sock, vector<char>& buffer) {
    clock_t start = clock();

    int cursorPosition = 0;
    int allocatedSize = buffer.size();
    const int THRESHOLD = 4096;

    fd_set fdRead;
    timeval timeout;
    timeout.tv_sec = 5;  // Timeout after 5 seconds
    timeout.tv_usec = 0;
    cout << "\tLoading...";
    while (true) {
        FD_ZERO(&fdRead);
        FD_SET(sock, &fdRead);
        int ret = select(sock + 1, &fdRead, NULL, NULL, &timeout);
        if (ret > 0) {
            int bytesReceived = recv(sock, buffer.data() + cursorPosition, allocatedSize - cursorPosition, 0);
            if (bytesReceived < 0) {

                double duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000;
                cout << " failed with " << WSAGetLastError() << " on recv\n";
                return false;
            }
            cursorPosition += bytesReceived;
            if (bytesReceived == 0) {
                double duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000;
                if (cursorPosition > 0 && strncmp(buffer.data(), "HTTP/", 5) != 0) {
                    cout << " failed with non-HTTP header (does not begin with HTTP/)\n";
                    return false;
                }
                cout << " done in " << duration << " ms with " << cursorPosition << " bytes\n";
                buffer[cursorPosition] = '\0';
                return true;
            }

            if (allocatedSize - cursorPosition < THRESHOLD) {
                allocatedSize *= 2;
                buffer.resize(allocatedSize);
            }
        }
        else if (ret == 0) {
            double duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000;
            cout << " failed with timeout\n";
            return false;
        }
        else {
            double duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000;
            cout << " failed with " << WSAGetLastError() << "\n";
            return false;
        }
    }
}

bool parseURL(const string& url, URLParts& parts) {
    cout << "URL: " << url << endl;
    cout << "\tParsing URL... ";
    size_t delimiter_end = url.find("http://") + 7;
    if (delimiter_end == string::npos) {
        cout << "failed with invalid scheme" << endl;
        return false;
    }

    parts.scheme = url.substr(0, delimiter_end - 3);
    string URL = url.substr(delimiter_end); // Remove scheme and ://

    size_t fragment_start = URL.find('#');
    if (fragment_start != string::npos) {
        URL = URL.substr(0, fragment_start);
    }

    size_t query_start = URL.find('?');
    if (query_start != string::npos) {
        parts.query = URL.substr(query_start + 1);
        URL = URL.substr(0, query_start);
    }

    size_t path_start = URL.find('/');
    if (path_start != string::npos) {
        parts.path = URL.substr(path_start);
        URL = URL.substr(0, path_start);
    }

    size_t port_start = URL.find(':');
    if (port_start != string::npos) {
        parts.host = URL.substr(0, port_start);
        try {
            parts.port = stoi(URL.substr(port_start + 1));
            // Ensure port is valid
            if (parts.port <= 0 || parts.port > 65535) {
                cout << "failed with invalid port " << endl;
                return false;
            }
        }
        catch (const invalid_argument& e) {
            cout << "failed with invalid port " << endl;
            return false;
        }
    }
    else {
        parts.host = URL;
    }

    cout << "host: " << parts.host << ", port: " << parts.port << ", path: " << parts.path << endl;
    return true;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printUsage();
        return 1;
    }

    URLParts parts;
    if (!parseURL(argv[1], parts)) {
        return 1;
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cout << "WSAStartup failed with error " << WSAGetLastError() << endl;
        return -1;
    }

    struct addrinfo* result = nullptr;
    if (!isIPAddress(parts.host)) {
        if (!resolveDNS(parts.host, to_string(parts.port), result)) {
            WSACleanup();
            return 1;
        }
    }
    else {
        // If it's an IP address
        result = new addrinfo;
        result->ai_family = AF_INET;
        result->ai_socktype = SOCK_STREAM;
        result->ai_protocol = IPPROTO_TCP;
        result->ai_addrlen = sizeof(sockaddr_in);
        sockaddr_in* addr = new sockaddr_in();
        addr->sin_family = AF_INET;
        inet_pton(AF_INET, parts.host.c_str(), &(addr->sin_addr));
        addr->sin_port = htons(parts.port);
        result->ai_addr = reinterpret_cast<sockaddr*>(addr);
        result->ai_next = nullptr;
        cout << "\tDoing DNS... done in 0 ms, found " << parts.host << endl;
    }

    SOCKET sock = createSocket();
    if (sock == INVALID_SOCKET) {
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    bool connected = connectToServer(sock, result);
    if (!connected) {
        freeaddrinfo(result);
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    string request = "GET " + parts.path + (parts.query.empty() ? "" : "?" + parts.query) + " HTTP/1.0\r\n";
    request += "Host: " + parts.host + "\r\n";
    request += "User-Agent: studentTAMUcrawler/1.0\r\n";
    request += "Connection: close\r\n\r\n";

    if (!sendRequest(sock, request)) {
        freeaddrinfo(result);
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    vector<char> buffer(4096);
    if (!receiveResponse(sock, buffer)) {
        freeaddrinfo(result);
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    string response(buffer.begin(), buffer.end());
    size_t header_end = response.find("\r\n\r\n");
    string headers = response.substr(0, header_end);
    string body = response.substr(header_end + 4);

    size_t status_line_end = response.find("\r\n");
    if (status_line_end != string::npos) {
        string status_line = response.substr(0, status_line_end);
        size_t status_code_start = status_line.find(" ") + 1; // Skip "HTTP/1.x "
        size_t status_code_end = status_line.find(" ", status_code_start);
        string status_code_str = status_line.substr(status_code_start, status_code_end - status_code_start);
        int status_code = stoi(status_code_str);

        cout << "\tVerifying header... status code " << status_code << "\n";

        if (status_code >= 200 && status_code < 300) {
            string base_url = "http://" + parts.host;
            clock_t start = clock();
            HTMLParserBase* parser = new HTMLParserBase();
            int nLinks;

            char* linkBuffer = parser->Parse(&body[0], body.length(), &base_url[0], base_url.length(), &nLinks);
            double duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000;
            cout << "\t+ Parsing page... done in " << duration << " ms with " << nLinks << " links:\n";
            delete parser;
        }
        // Print the headers
        cout << "----------------------------------------\n";
        cout << headers << "\n";
    }
    
    
    //freeaddrinfo(result); check this later
    closesocket(sock);
    WSACleanup();
    return 0;
}
