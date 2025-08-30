//Semester: Spring/2025
//Course: CSCE 612
//Hw1P2

#include "pch.h"

using namespace std;
set<string> seenHosts;
set<string> seenIPs;

struct URLParts {
    string scheme;
    string host;
    int port = 80;  // Default to HTTP port 80
    string path = "/";   // Default to root path
    string query;
    string fragment;
};

bool isIPAddress(const string& host) {
    struct sockaddr_in sa;
    return inet_pton(AF_INET, host.c_str(), &(sa.sin_addr)) == 1;
}

bool isHostUnique(const string& host) {
    auto result = seenHosts.insert(host);
    return result.second;
}

bool isIPUnique(const string& ip) {
    auto result = seenIPs.insert(ip);
    return result.second;
}

bool resolveDNS(const string& host, const string& service, struct addrinfo*& result) {
    if (!isIPAddress(host)) {
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
    else {
        result = new addrinfo;
        result->ai_family = AF_INET;
        result->ai_socktype = SOCK_STREAM;
        result->ai_protocol = IPPROTO_TCP;
        result->ai_addrlen = sizeof(sockaddr_in);
        sockaddr_in* addr = new sockaddr_in();
        addr->sin_family = AF_INET;
        inet_pton(AF_INET, host.c_str(), &(addr->sin_addr));
        addr->sin_port = htons(stoi(service));
        result->ai_addr = reinterpret_cast<sockaddr*>(addr);
        result->ai_next = nullptr;
        cout << "\tDoing DNS... done in 0 ms, found " << host << endl;
    }
    return true;
}

SOCKET createSocket() {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        cout << "\tSocket creation failed with error " << WSAGetLastError() << endl;
        return INVALID_SOCKET;
    }
    return sock;
}

bool connectToServer(SOCKET sock, struct addrinfo* result, const string& connectionType) {
    clock_t start = clock();
    cout << "\t* " << connectionType << "... ";  // Use the connectionType in the output
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
bool receiveResponse(SOCKET sock, vector<char>& buffer, size_t maxSize, int timeoutSec, boolean isMultipleURLs) {
    clock_t start = clock();
    int cursorPosition = 0;
    int allocatedSize = buffer.size();
    const int THRESHOLD = 4096;
    int totalBytesReceived = 0;
    

    fd_set fdRead;
    timeval timeout;
    timeout.tv_sec = timeoutSec;
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
            totalBytesReceived += bytesReceived;

            if (bytesReceived == 0) {
                double duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000;
                if (cursorPosition > 0 && strncmp(buffer.data(), "HTTP/", 5) != 0) {
                    cout << " failed with non-HTTP header (does not begin with HTTP/)\n";
                    return false;
                }
                cout << " done in " << duration << " ms with " << totalBytesReceived << " bytes\n";
                buffer[cursorPosition] = '\0';  // Ensure null-termination for string processing
                return true;
            }

            if (isMultipleURLs) {
                double duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000;
                if (duration > 10000) {
                    cout << " failed with slow download\n";
                    return false;
                }
            }
                if (allocatedSize - cursorPosition < THRESHOLD) {
                    allocatedSize *= 2;
                    if (buffer.size() < maxSize) {
                        buffer.resize(min(allocatedSize, maxSize));  // Prevent buffer from exceeding max size
                    }
                    else {
                        cout << " failed with exceeding max\n";
                        return false;
                    }
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


bool parseURL(const string& url, URLParts& parts, boolean singleURL) {
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
    if(singleURL)
        cout << "host: " << parts.host << ", port: " << parts.port << ", path: " << parts.path << endl;
    else
        cout << "host: " << parts.host << ", port: " << parts.port << endl;
    return true;
}

int main(int argc, char* argv[]) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cout << "WSAStartup failed with error " << WSAGetLastError() << endl;
        return -1;
    }

    if (argc == 2) {
        URLParts parts;
        if (!parseURL(argv[1], parts, true)) {
            return 1;
        }

        struct addrinfo* result = nullptr;
        if (!resolveDNS(parts.host, to_string(parts.port), result)) {
            WSACleanup();
            return 1;
        }

        SOCKET sock = createSocket();
        if (sock == INVALID_SOCKET) {
            WSACleanup();
            return 1;
        }

        bool connected = connectToServer(sock, result, "Connecting on page");
        if (!connected) {
            closesocket(sock);
            WSACleanup();
            return 1;
        }

        string request = "GET " + parts.path + (parts.query.empty() ? "" : "?" + parts.query) + " HTTP/1.0\r\n";
        request += "Host: " + parts.host + "\r\n";
        request += "User-Agent: studentTAMUcrawler/1.0\r\n";
        request += "Connection: close\r\n\r\n";

        if (!sendRequest(sock, request)) {
            closesocket(sock);
            WSACleanup();
            return 1;
        }

        vector<char> buffer(32768);
        if (!receiveResponse(sock, buffer, 2 * 1024 * 1024 * 1024, 10,false)) {
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
    //Part 2
    else if (argc == 3) {
        if (string(argv[1]) != "1") {
            cout << "Usage: " << argv[0] << " 1 [URL-input.txt]" << endl;
            return 1;
        }

        string filename = argv[2];
        ifstream file(filename);
        if (!file.is_open()) {
            cout << "Error: Unable to open file " << filename << endl;
            return 1;
        }

        file.seekg(0, ios::end);
        streamsize size = file.tellg();  // Get the file size
        file.seekg(0, ios::beg);  // Reset to start of the file for reading

        // Load the entire file into memory
        string content((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
        file.close();

        cout << "Opened " << filename << " with size " << size << " bytes" << endl;
        // Split the content into lines
        stringstream ss(content);
        string line;
        while (getline(ss, line)) {
            URLParts parts;
            if (!parseURL(line, parts, false)) {
                continue;
            }

            // Check host uniqueness
            if (!isHostUnique(parts.host)) {
                cout << "\tChecking host uniqueness... failed" << endl;
                cout << "\n";
                continue;
            }
            cout << "\tChecking host uniqueness... passed" << endl;

            struct addrinfo* result = nullptr;
            if (!resolveDNS(parts.host, to_string(parts.port), result)) {
                cout << "\n";
                continue;
            }

            char ipstr[INET_ADDRSTRLEN];
            auto* ipv4 = (struct sockaddr_in*)result->ai_addr;
            inet_ntop(AF_INET, &(ipv4->sin_addr), ipstr, sizeof(ipstr));
            if (!isIPUnique(ipstr)) {
                cout << "\tChecking IP uniqueness... failed" << endl;
                cout << "\n";
                continue;
            }
            cout << "\tChecking IP uniqueness... passed" << endl;

            SOCKET sockRobots = createSocket();
            if (sockRobots == INVALID_SOCKET) {
                WSACleanup();
                return 1;
            }

            bool connectedRobots = connectToServer(sockRobots, result, "Connecting on robots.txt");
            if (!connectedRobots) {
                closesocket(sockRobots);
                WSACleanup();
                continue;
            }

            string requestRobots = "HEAD /robots.txt HTTP/1.0\r\n";
            requestRobots += "Host: " + parts.host + "\r\n";
            requestRobots += "User-Agent: studentTAMUcrawler/1.0\r\n";
            requestRobots += "Connection: close\r\n\r\n";

            if (!sendRequest(sockRobots, requestRobots)) {
                closesocket(sockRobots);
                WSACleanup();
                continue;
            }

            vector<char> bufferRobots(16384);
            if (!receiveResponse(sockRobots, bufferRobots, 16 * 1024, 10, true)) {
                closesocket(sockRobots);
                WSACleanup();
                continue;
            }
            
            closesocket(sockRobots);

            string responseRobots(bufferRobots.begin(), bufferRobots.end());
            size_t header_endRobots = responseRobots.find("\r\n\r\n");
            string headersRobots = responseRobots.substr(0, header_endRobots);
            string bodyRobots = responseRobots.substr(header_endRobots + 4);

            size_t status_line_endRobots = responseRobots.find("\r\n");
            if (status_line_endRobots != string::npos) {
                string status_lineRobots = responseRobots.substr(0, status_line_endRobots);
                size_t status_code_startRobots = status_lineRobots.find(" ") + 1; // Skip "HTTP/1.x "
                size_t status_code_endRobots = status_lineRobots.find(" ", status_code_startRobots);
                string status_code_strRobots = status_lineRobots.substr(status_code_startRobots, status_code_endRobots - status_code_startRobots);
                int status_codeRobots = stoi(status_code_strRobots);
                closesocket(sockRobots);
                bufferRobots.clear();
                cout << "\tVerifying header... status code " << status_codeRobots << "\n";

                if (status_codeRobots >= 400 && status_codeRobots < 500) {
                    SOCKET sockPage = createSocket();
                    if (sockPage == INVALID_SOCKET) {
                        WSACleanup();
                        continue;
                    }

                    bool connectedPage = connectToServer(sockPage, result, "Connecting on Page");
                    if (!connectedPage) {
                        WSACleanup();
                        continue;
                    }

                    string requestPage = "GET " + parts.path + " HTTP/1.0\r\n";
                    requestPage += "Host: " + parts.host + "\r\n";
                    requestPage += "User-Agent: studentTAMUcrawler/1.0\r\n";
                    requestPage += "Connection: close\r\n\r\n";

                    if (!sendRequest(sockPage, requestPage)) {
                        closesocket(sockPage);
                        WSACleanup();
                        continue;
                    }

                    vector<char> bufferPage(32768);
                    if (!receiveResponse(sockPage, bufferPage, 2 * 1024 * 1024, 10, true)) {
                        closesocket(sockPage);
                        WSACleanup();
                        continue;
                    }
                    string responsePage(bufferPage.begin(), bufferPage.end());
                    size_t header_endPage = responsePage.find("\r\n\r\n");
                    string headersPage = responsePage.substr(0, header_endPage);
                    string bodyPage = responsePage.substr(header_endPage + 4);

                    size_t status_line_endPage = responsePage.find("\r\n");
                    if (status_line_endPage != string::npos) {
                        string status_linePage = responsePage.substr(0, status_line_endPage);
                        size_t status_code_startPage = status_linePage.find(" ") + 1; // Skip "HTTP/1.x "
                        size_t status_code_endPage = status_linePage.find(" ", status_code_startPage);
                        string status_code_strPage = status_linePage.substr(status_code_startPage, status_code_endPage - status_code_startPage);
                        int status_codePage = stoi(status_code_strPage);

                        cout << "\tVerifying header... status code " << status_codePage << "\n";

                        if (status_codePage >= 200 && status_codePage < 300) {
                            string base_urlPage = "http://" + parts.host;
                            clock_t startPage = clock();
                            HTMLParserBase* parserPage = new HTMLParserBase();
                            int nLinksPage;

                            char* linkBufferPage = parserPage->Parse(&bodyPage[0], bodyPage.length(), &base_urlPage[0], base_urlPage.length(), &nLinksPage);
                            double durationPage = (double)(clock() - startPage) / CLOCKS_PER_SEC * 1000;
                            cout << "\t+ Parsing page... done in " << durationPage << " ms with " << nLinksPage << " links\n";

                            delete parserPage;
                        }
                    }
                    bufferPage.clear();
                }
            }
        }
    }
    else {
        // Incorrect number of arguments
        cout << "Usage: " << argv[0] << " [URL] or " << argv[0] << " 1 [URL-input.txt]" << endl;
        return 1;
    }

    WSACleanup();
    return 0;
}
