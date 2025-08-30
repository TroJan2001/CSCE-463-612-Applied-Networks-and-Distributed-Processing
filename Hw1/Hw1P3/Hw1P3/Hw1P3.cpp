//Semester: Spring/2025
//Course: CSCE 612
//Hw1P3

#include "pch.h"
#include <chrono>

using namespace std;


set<string> seenHosts;
set<string> seenIPs;
atomic<bool> isMultipleURLs = false;
mutex seenHostsMutex;
mutex seenIPsMutex;
atomic<bool> isCrawlingFinished = false;

struct URLParts {
	string scheme;
	string host;
	int port = 80;  // Default to HTTP port 80
	string path = "/";   // Default to root path
	string query;
	string fragment;
};

struct HttpResponse {
	string headers;
	string body;
	int statusCode = 0;
};

struct CrawlerStats {
	atomic<long int> totalBytesDownloaded{ 0 };
	atomic<int> http2xx{ 0 };
	atomic<int> http3xx{ 0 };
	atomic<int> http4xx{ 0 };
	atomic<int> http5xx{ 0 };
	atomic<int> httpOthers{ 0 };

	atomic<int> activeThreadsCount{ 0 };
	atomic<int> currentQueueSize{ 0 };
	atomic<int> numExtractedURLs{ 0 };
	atomic<int> numUniqueHost{ 0 };
	atomic<int> numDNSLookups{ 0 };
	atomic<int> numUniqueIP{ 0 };
	atomic<int> numPassedRobots{ 0 };
	atomic<int> numCrawledURLs{ 0 };
	atomic<int> totalLinksFound{ 0 };

	atomic<int> externalOriginTAMULinks{ 0 };
	atomic<int> pagesWithValidTAMULinks{ 0 };

	void incrementctiveThreadsCount() {
		activeThreadsCount.fetch_add(1, memory_order_relaxed);
	}
	void incrementExtractedURLs() {
		numExtractedURLs.fetch_add(1, memory_order_relaxed);
	}
	void incrementUniqueHost() {
		numUniqueHost.fetch_add(1, memory_order_relaxed);
	}
	void incrementDNSLookups() {
		numDNSLookups.fetch_add(1, memory_order_relaxed);
	}
	void incrementUniqueIP() {
		numUniqueIP.fetch_add(1, memory_order_relaxed);
	}
	void incrementPassedRobots() {
		numPassedRobots.fetch_add(1, memory_order_relaxed);
	}
	void incrementCrawledURLs() {
		numCrawledURLs.fetch_add(1, memory_order_relaxed);
	}
	void incrementPagesWithValidTAMULinks() {
		pagesWithValidTAMULinks.fetch_add(1, memory_order_relaxed);
	}
	void incrementExternalOriginTAMULinks() {
		externalOriginTAMULinks.fetch_add(1, memory_order_relaxed);
	}
	void incrementTotalLinksFound(int count) {
		totalLinksFound.fetch_add(count, memory_order_relaxed);
	}
	void incrementTotalBytesDownloaded(int count) {
		totalBytesDownloaded.fetch_add(count, memory_order_relaxed);
	}
};

CrawlerStats stats;

string createHttpRequest(const string& method, const string& host, const string path) {
	string request = method + " " + path + " HTTP/1.0\r\n";
	request += "Host: " + host + "\r\n";  // Host header
	request += "User-Agent: studentTAMUcrawler/1.0\r\n";
	request += "Connection: close\r\n\r\n";
	return request;
}

HttpResponse parseHttpResponse(const vector<char>& buffer, const URLParts& parts) {
	string response(buffer.begin(), buffer.end());
	size_t header_end = response.find("\r\n\r\n");
	HttpResponse result;

	if (header_end != string::npos) {
		result.headers = response.substr(0, header_end);
		result.body = response.substr(header_end + 4);

		size_t status_line_end = response.find("\r\n");
		if (status_line_end != string::npos) {
			string status_line = response.substr(0, status_line_end);
			size_t status_code_start = status_line.find(" ") + 1;
			size_t status_code_end = status_line.find(" ", status_code_start);
			if (status_code_start != string::npos && status_code_end != string::npos) {
				string status_code_str = status_line.substr(status_code_start, status_code_end - status_code_start);
				result.statusCode = stoi(status_code_str);
				if (!isMultipleURLs)
					cout << "\tVerifying header... status code " << result.statusCode << "\n";
			}
		}
	}
	return result;
}

void parsePageLinks(HttpResponse httpResponse, const URLParts& parts) {
	string base_urlPage = "http://" + parts.host;
	clock_t startPage = clock();
	HTMLParserBase* pageParser = new HTMLParserBase();
	int nLinksPage;

	char* linkBufferPage = pageParser->Parse(&httpResponse.body[0], static_cast<int>(httpResponse.body.length()), &base_urlPage[0], static_cast<int>(base_urlPage.length()), &nLinksPage);
	
	regex tamuRegex(R"(^http:\/\/(?:.*\.)?tamu\.edu\b(?:\/.*)?)");
	for (int i = 0; i < nLinksPage; i++) {
		string link(linkBufferPage);
		if (regex_match(link, tamuRegex)) {
			stats.incrementPagesWithValidTAMULinks();
			string origin = "http://" + parts.host;
			if (!regex_match(origin, tamuRegex)) {
				stats.incrementExternalOriginTAMULinks();
			}
			break;
		}
		linkBufferPage += strlen(linkBufferPage) + 1;
	}
	double durationPage = (double)(clock() - startPage) / CLOCKS_PER_SEC * 1000;
	if (!isMultipleURLs)
		cout << "\t+ Parsing page... done in " << durationPage << " ms with " << nLinksPage << " links\n";
	else {
		stats.incrementTotalLinksFound(nLinksPage);
	}
	delete pageParser;
}

bool isIPAddress(const string& host) {
	struct sockaddr_in sa;
	return inet_pton(AF_INET, host.c_str(), &(sa.sin_addr)) == 1;
}

bool isHostUnique(const string& host) {
	seenHostsMutex.lock();
	auto result = seenHosts.insert(host);
	seenHostsMutex.unlock();
	return result.second;
}

bool isIPUnique(const string& ip) {
	seenIPsMutex.lock();
	auto result = seenIPs.insert(ip);
	seenIPsMutex.unlock();
	return result.second;
}

bool resolveDNS(const string& host, const string& service, struct addrinfo*& result) {
	struct addrinfo hints = {};
	hints.ai_family = AF_INET;        // IPv4
	hints.ai_socktype = SOCK_STREAM;  // TCP
	hints.ai_protocol = IPPROTO_TCP;

	if (isIPAddress(host)) {
		auto* addr = new sockaddr_in{};
		addr->sin_family = AF_INET;
		inet_pton(AF_INET, host.c_str(), &addr->sin_addr);
		addr->sin_port = htons(stoi(service));

		result = new addrinfo;
		result->ai_family = AF_INET;
		result->ai_socktype = SOCK_STREAM;
		result->ai_protocol = IPPROTO_TCP;
		result->ai_addrlen = sizeof(sockaddr_in);
		result->ai_addr = reinterpret_cast<sockaddr*>(addr);
		result->ai_next = nullptr;

		if (!isMultipleURLs)
			cout << "\tDoing DNS... done in 0 ms, found " << host << endl;
	}
	else {
		auto start = chrono::high_resolution_clock::now();
		if (!isMultipleURLs)
			cout << "\tDoing DNS... ";

		int res = getaddrinfo(host.c_str(), service.c_str(), &hints, &result);
		auto end = chrono::high_resolution_clock::now();
		chrono::duration<double, milli> duration = end - start;

		if (res != 0) {
			if (!isMultipleURLs)
				cout << "failed with error " << gai_strerror(res) << endl;
			return false;
		}

		if (!isMultipleURLs) {
			char ipstr[INET_ADDRSTRLEN];
			auto* ipv4 = (struct sockaddr_in*)result->ai_addr;
			inet_ntop(AF_INET, &(ipv4->sin_addr), ipstr, sizeof(ipstr));
			cout << "done in " << duration.count() << " ms, found " << ipstr << endl;
		}
	}
	if (isMultipleURLs)
		stats.incrementDNSLookups();
	return true;
}



SOCKET createSocket() {
	SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET) {
		if (!isMultipleURLs)
			cout << "\tSocket creation failed with error " << WSAGetLastError() << endl;
		return INVALID_SOCKET;
	}
	return sock;
}

bool connectToServer(SOCKET sock, struct addrinfo* result, const string& connectionType) {
	clock_t start = clock();
	if (!isMultipleURLs)
		cout << "\t* " << connectionType << "... ";
	for (struct addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
		int addrLen = static_cast<int>(ptr->ai_addrlen);
		if (ptr->ai_addrlen > static_cast<size_t>(INT_MAX)) {
			continue;
		}
		int connectStatus = connect(sock, ptr->ai_addr, addrLen);
		clock_t end = clock();
		double elapsed = static_cast<double>(end - start) / CLOCKS_PER_SEC * 1000;

		if (connectStatus == 0) {
			if (!isMultipleURLs)
				cout << "done in " << elapsed << "ms" << endl;
			return true;
		}
		if (!isMultipleURLs)
			cout << "failed with " << WSAGetLastError() << endl;
	}
	return false;
}

bool sendRequest(SOCKET sock, const string& request) {
	int requestLength = static_cast<int>(request.length());
	if (request.length() > INT_MAX) {
		return false;
	}
	if (send(sock, request.c_str(), requestLength, 0) == SOCKET_ERROR) {
		if (!isMultipleURLs)
			cout << "Failed with " << WSAGetLastError() << endl;
		return false;
	}
	return true;
}

bool receiveResponse(SOCKET sock, vector<char>& buffer, size_t maxSize, int timeoutSec) {
	clock_t start = clock();
	int cursorPosition = 0;
	int allocatedSize = static_cast<int> (buffer.size());
	const int THRESHOLD = 2048;
	int totalBytesReceived = 0;


	fd_set fdRead{};
	timeval timeout{};
	timeout.tv_sec = timeoutSec;
	timeout.tv_usec = 0;

	if (!isMultipleURLs)
		cout << "\tLoading...";
	while (true) {
		FD_ZERO(&fdRead);
		FD_SET(sock, &fdRead);

		int ret = select(sock + 1, &fdRead, NULL, NULL, &timeout);
		if (sock == INVALID_SOCKET || sock > INT_MAX) {
			return false;
		}

		if (ret > 0) {
			int bytesReceived = recv(sock, buffer.data() + cursorPosition, allocatedSize - cursorPosition, 0);
			if (bytesReceived < 0) {
				double duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000;
				if (!isMultipleURLs)
					cout << " failed with " << WSAGetLastError() << " on recv\n";
				return false;
			}

			cursorPosition += bytesReceived;
			totalBytesReceived += bytesReceived;
			if (isMultipleURLs)
				stats.incrementTotalBytesDownloaded(bytesReceived);

			if (bytesReceived == 0) {
				double duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000;
				if (cursorPosition > 0 && strncmp(buffer.data(), "HTTP/", 5) != 0) {
					if (!isMultipleURLs)
						cout << " failed with non-HTTP header (does not begin with HTTP/)\n";
					return false;
				}
				if (!isMultipleURLs)
					cout << " done in " << duration << " ms with " << totalBytesReceived << " bytes\n";
				buffer[cursorPosition] = '\0';  // Ensure null-termination for string processing
				return true;
			}

			if (isMultipleURLs) {
				double duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000;
				if (duration > 10000) {
					return false;
				}
			}
			if (allocatedSize - cursorPosition < THRESHOLD) {
				allocatedSize *= 2;
				if (buffer.size() < maxSize) {
					buffer.resize(min(allocatedSize, maxSize));  // Prevent buffer from exceeding max size
				}
				else {
					return false;
				}
			}

		}
		else if (ret == 0) {
			double duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000;
			if (!isMultipleURLs)
				cout << " failed with timeout\n";
			return false;
		}
		else {
			double duration = (double)(clock() - start) / CLOCKS_PER_SEC * 1000;
			if (!isMultipleURLs)
				cout << " failed with " << WSAGetLastError() << "\n";
			return false;
		}
	}
}

bool parseURL(const string& url, URLParts& parts) {
	if (!isMultipleURLs) {
		cout << "URL: " << url << endl;
		cout << "\tParsing URL... ";
	}
	size_t delimiter_end = url.find("http://") + 7;
	if (delimiter_end == string::npos) {
		if (!isMultipleURLs)
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
				if (!isMultipleURLs)
					cout << "failed with invalid port " << endl;
				return false;
			}
		}
		catch (const invalid_argument& e) {
			if (!isMultipleURLs)
				cout << "failed with invalid port " << endl;
			return false;
		}
	}
	else {
		parts.host = URL;
	}
	if (!isMultipleURLs)
		cout << "host: " << parts.host << ", port: " << parts.port << ", path: " << parts.path << endl;
	return true;
}

bool handleSocketInteraction(struct addrinfo* result, const string& host, const string& path, const string& method, int maxSize, int timeoutSec, bool isMultipleURLs, vector<char>& buffer) {
	// Create socket
	SOCKET sock = createSocket();
	if (sock == INVALID_SOCKET) {
		cout << "\tSocket creation failed with error " << WSAGetLastError() << endl;
		return false;
	}

	// Connect to server
	string pageType = (method == "GET" ? "page" : "robots.txt");
	string connectionType = "Connecting on " + pageType;
	if (!connectToServer(sock, result, connectionType)) {
		closesocket(sock);
		return false;
	}

	// Create HTTP request
	string request = createHttpRequest(method, host, path);
	if (!sendRequest(sock, request)) {
		closesocket(sock);
		return false;
	}

	// Receive response
	if (!receiveResponse(sock, buffer, maxSize, timeoutSec)) {
		closesocket(sock);
		return false;
	}

	closesocket(sock);
	return true;
}


streamsize ReadFileAndPopulateQueue(const string& filename, queue<URLParts>& urlQueue) {
	ifstream file(filename, ios::binary | ios::ate);
	if (!file.is_open()) {
		cerr << "Failed to open file: " << filename << endl;
		return -1;
	}

	streamsize size = file.tellg();
	file.seekg(0, ios::beg);

	string line;
	while (getline(file, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}

		URLParts parts;
		if (parseURL(line, parts)) {
			urlQueue.push(parts);
		}
	}
	file.close();
	return size;
}

void processURLs(mutex& queueMutex, queue<URLParts>& urlQueue) {
	stats.activeThreadsCount.fetch_add(1, memory_order_relaxed);
	while (true) {
		queueMutex.lock();
		if (urlQueue.empty()) {
			isCrawlingFinished = true;
			queueMutex.unlock();
			stats.activeThreadsCount.fetch_sub(1, memory_order_relaxed);
			return;
		}
		URLParts parts = urlQueue.front();
		urlQueue.pop();
		queueMutex.unlock();
		stats.incrementExtractedURLs();

		if (!isHostUnique(parts.host)) {
			continue;
		}
		stats.incrementUniqueHost();

		struct addrinfo* result = nullptr;
		if (!resolveDNS(parts.host, to_string(parts.port), result)) {
			if (!isMultipleURLs)
				cout << "\n";

			if (result) {
				if (result->ai_addr) delete result->ai_addr;
				delete result;
			}
			continue;
		}

		char ipstr[INET_ADDRSTRLEN];
		auto* ipv4 = (struct sockaddr_in*)result->ai_addr;
		inet_ntop(AF_INET, &(ipv4->sin_addr), ipstr, sizeof(ipstr));

		if (!isIPUnique(ipstr)) {
			if (result->ai_addr) delete result->ai_addr;
			delete result;
			continue;
		}
		stats.incrementUniqueIP();

		vector<char> robotsBuffer(16384);
		if (!handleSocketInteraction(result, parts.host, "/robots.txt", "HEAD", 16 * 1024, 10, true, robotsBuffer)) {

			if (result->ai_addr) delete result->ai_addr;
			delete result;
			continue;
		}
		HttpResponse robotsResponse = parseHttpResponse(robotsBuffer, parts);
		robotsBuffer.clear();
		if (robotsResponse.statusCode >= 400 && robotsResponse.statusCode < 500) {
			stats.incrementPassedRobots();
			vector<char> bufferPage(32768);
			if (!handleSocketInteraction(result, parts.host, parts.path, "GET", 2 * 1024 * 1024, 10, true, bufferPage)) {
				if (result->ai_addr) delete result->ai_addr;
				delete result;
				continue;
			}
			HttpResponse responsePage = parseHttpResponse(bufferPage, parts);
			bufferPage.clear();
			stats.incrementCrawledURLs();

			switch (responsePage.statusCode / 100) {
			case 2: stats.http2xx.fetch_add(1, memory_order_relaxed); break;
			case 3: stats.http3xx.fetch_add(1, memory_order_relaxed); break;
			case 4: stats.http4xx.fetch_add(1, memory_order_relaxed); break;
			case 5: stats.http5xx.fetch_add(1, memory_order_relaxed); break;
			default: stats.httpOthers.fetch_add(1, memory_order_relaxed); break;
			}
			if (responsePage.statusCode >= 200 && responsePage.statusCode < 300) {
				parsePageLinks(responsePage, parts);
			}
		}

		if (result->ai_addr) delete result->ai_addr;
		delete result;
	}
}

double calculateRate(long double totalCount, double totalTimeInSec) {
	return static_cast<double>(totalCount) / totalTimeInSec;
}

void printFinalStats(double totalTime) {
	cout << "\nExtracted " << stats.numExtractedURLs.load() << " URLs @ "
		<< calculateRate(stats.numExtractedURLs.load(), totalTime) << "/s" << endl;
	cout << "Looked up " << stats.numUniqueHost.load() << " DNS names @ "
		<< calculateRate(stats.numUniqueHost.load(), totalTime) << "/s" << endl;
	cout << "Attempted " << stats.numUniqueIP.load() << " robots @ "
		<< calculateRate(stats.numUniqueIP.load(), totalTime) << "/s" << endl;
	cout << "Crawled " << stats.numCrawledURLs.load() << " pages @ "
		<< calculateRate(stats.numCrawledURLs.load(), totalTime) << "/s (" << stats.totalBytesDownloaded.load() / (1024 * 1024) << "MB)" << endl;
	cout << "Parsed " << stats.totalLinksFound.load() << " links @ "
		<< calculateRate(stats.totalLinksFound.load(), totalTime) << "/s" << endl;
	cout << "HTTP codes: 2xx = " << stats.http2xx << ", 3xx = " << stats.http3xx << ", 4xx = " << stats.http4xx <<
		", 5xx =" << stats.http5xx << ", other = " << stats.httpOthers << endl;
}

void printStatsPeriodically(mutex& queueMutex, queue<URLParts>& urlQueue) {
	int oldBytesDownloaded = 0;
	int oldCrawledURLs = 0;
	int totalCrawledURLs = 0;
	long int totalBytesDownloaded = 0;
	int timer = 2;
	long double crawledURLsRate;
	long double downladRate;
	clock_t startTime = clock();
	clock_t lastTime = clock();
	while (true) {
		this_thread::sleep_for(chrono::seconds(2));
		clock_t currentTime = clock();
		double elapsedSeconds = (currentTime - lastTime) / (double)CLOCKS_PER_SEC;
		cout << "[" << setw(3) << timer << "]";
		queueMutex.lock();
		int currentQueueSize = static_cast<int> (urlQueue.size());
		queueMutex.unlock();
		cout << setw(4) << stats.activeThreadsCount.load();
		cout << " Q" << setw(6) << currentQueueSize;
		cout << " E" << setw(7) << stats.numExtractedURLs.load();
		cout << " H" << setw(6) << stats.numUniqueHost.load();
		cout << " D" << setw(6) << stats.numDNSLookups.load();
		cout << " I" << setw(5) << stats.numUniqueIP.load();
		cout << " R" << setw(5) << stats.numPassedRobots.load();
		cout << " C" << setw(5) << stats.numCrawledURLs.load();
		cout << " L" << setw(4) << stats.totalLinksFound.load() / 1000 << "K" << endl;
		totalCrawledURLs = stats.numCrawledURLs.load();
		totalBytesDownloaded = stats.totalBytesDownloaded.load();
		crawledURLsRate = calculateRate(totalCrawledURLs - oldCrawledURLs, elapsedSeconds);
		downladRate = (calculateRate(totalBytesDownloaded - oldBytesDownloaded, elapsedSeconds) * 8) / (1024 * 1024);
		cout << "\t*** crawling " << fixed << setprecision(1) << crawledURLsRate << " pps @ " << fixed << setprecision(1) << downladRate << " Mbps" << endl;
		oldCrawledURLs = totalCrawledURLs;
		oldBytesDownloaded = totalBytesDownloaded;
		lastTime = currentTime;
		timer += 2;
		if (isCrawlingFinished) {
			clock_t endTime = clock();
			queueMutex.lock();
			int currentQueueSize = static_cast<int>(urlQueue.size());
			queueMutex.unlock();
			cout << "[" << setw(3) << timer << "]";
			cout << setw(4) << stats.activeThreadsCount.load();
			cout << " Q" << setw(6) << currentQueueSize;
			cout << " E" << setw(7) << stats.numExtractedURLs.load();
			cout << " H" << setw(6) << stats.numUniqueHost.load();
			cout << " D" << setw(6) << stats.numDNSLookups.load();
			cout << " I" << setw(5) << stats.numUniqueIP.load();
			cout << " R" << setw(5) << stats.numPassedRobots.load();
			cout << " C" << setw(5) << stats.numCrawledURLs.load();
			cout << " L" << setw(4) << stats.totalLinksFound.load() / 1000 << "K" << endl;
			totalCrawledURLs = stats.numCrawledURLs.load();
			totalBytesDownloaded = stats.totalBytesDownloaded.load();
			crawledURLsRate = calculateRate(totalCrawledURLs - oldCrawledURLs, elapsedSeconds);
			downladRate = (calculateRate(totalBytesDownloaded - oldBytesDownloaded, elapsedSeconds) * 8) / (1024 * 1024);
			cout << "\t*** crawling " << crawledURLsRate << " pps @ " << downladRate << " Mbps" << endl;
			oldCrawledURLs = totalCrawledURLs;
			oldBytesDownloaded = totalBytesDownloaded;
			double elapsedSeconds = static_cast<double>(endTime - startTime) / CLOCKS_PER_SEC;
			printFinalStats(elapsedSeconds);
			cout << "Total Pages with valid TAMU links found: " << stats.pagesWithValidTAMULinks.load() << endl;
			cout << "Total Pages with valid TAMU links originating from outside TAMU: " << stats.externalOriginTAMULinks.load() << endl;
			return;
		}
	}
}


int main(int argc, char* argv[]) {

	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		cout << "WSAStartup failed with error " << WSAGetLastError() << endl;
		return -1;
	}

	if (argc == 2) {
		URLParts parts;
		if (!parseURL(argv[1], parts)) {
			return 1;
		}

		struct addrinfo* result = nullptr;
		if (!resolveDNS(parts.host, to_string(parts.port), result)) {
			WSACleanup();
			return 1;
		}
		vector<char> buffer(32768);
		if (!handleSocketInteraction(result, parts.host, parts.path, "GET", 2 * 1024 * 1024 * 512, 10, false, buffer))
			return 1;
		HttpResponse pageResponse = parseHttpResponse(buffer, parts);
		buffer.clear();
		if (pageResponse.statusCode >= 200 && pageResponse.statusCode < 300) {
			parsePageLinks(pageResponse, parts);
		}
		// Print the headers
		cout << "----------------------------------------\n";
		cout << pageResponse.headers << "\n";
		WSACleanup();
		return 0;
	}
	//Part 2&3
	else if (argc == 3) {
		isMultipleURLs = true;
		queue<URLParts> urlQueue;
		mutex queueMutex;
		int numberOfThreads = stoi(argv[1]);
		string filename = argv[2];
		ifstream file(filename);

		streamsize size = ReadFileAndPopulateQueue(filename, urlQueue);  // Get the file size

		cout << "Opened " << filename << " with size " << size << " bytes" << endl;

		thread statsThread(printStatsPeriodically, ref(queueMutex), ref(urlQueue));
		SetThreadPriority(statsThread.native_handle(), THREAD_PRIORITY_ABOVE_NORMAL);
		vector<thread> threads;
		for (int i = 0; i < numberOfThreads; i++) {
			threads.push_back(thread(processURLs, ref(queueMutex), ref(urlQueue)));
		}
		for (auto& th : threads) {
			th.join();
		}
		statsThread.join();
	}
	else {
		// Incorrect number of arguments
		cout << "Usage: " << argv[0] << " [URL] or " << argv[0] << " [Number of Threads] [URL-input.txt]" << endl;
		return 1;
	}
	WSACleanup();
	return 0;
}
