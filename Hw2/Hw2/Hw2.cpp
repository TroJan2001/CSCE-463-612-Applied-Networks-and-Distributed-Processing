//Semester: Spring/2025
//Course: CSCE 612
//Hw2

//This code has the extra credit part implemented

#include "pch.h"

using namespace std;

#pragma pack(push, 1)
struct DNSHeader {
    unsigned short TXID;
    unsigned short flags;
    unsigned short qdCount;
    unsigned short anCount;
    unsigned short nsCount;
    unsigned short arCount;
};

struct DNSQuestion {
    unsigned short qType;
    unsigned short qClass;
};

struct DNSAnswerHeader {
    unsigned short type;
    unsigned short _class;
    unsigned int TTL;
    unsigned short dataLength;
};
#pragma pack(pop)


// Function to convert type numbers to readable strings
const char* dnsTypeToString(unsigned short type) {
    switch (type) {
    case DNS_A: return "A";
    case DNS_NS: return "NS";
    case DNS_CNAME: return "CNAME";
    case DNS_PTR: return "PTR";
    default: return "UNKNOWN";
    }
}

void convertIPtoPTR(char* ptrQuery, const char* ipAddr) {
    struct in_addr addr;
    if (inet_pton(AF_INET, ipAddr, &addr) != 1) {
        printf("Invalid IP address format.\n");
        return;
    }

    unsigned char* bytes = (unsigned char*)&addr;
    sprintf_s(ptrQuery, 256, "%d.%d.%d.%d.in-addr.arpa",
        bytes[3], bytes[2], bytes[1], bytes[0]);
}


void convertToDNSFormat(char* dns, const char* hostname) {
    char* label = dns;
    const char* host = hostname;
    while (*host) {
        char* len = label++;
        while (*host && *host != '.') {
            *label++ = *host++;
        }
        *len = label - len - 1;
        if (*host) host++;
    }
    *label = 0;
}

void createDNSQuery(char* buffer, const char* formattedName, bool isPTR) {
    DNSHeader* header = (DNSHeader*)buffer;
    header->TXID = htons(rand() % 65536);
    header->flags = htons(0x0100); // Standard query with recursion desired
    header->qdCount = htons(1);
    header->anCount = 0;
    header->nsCount = 0;
    header->arCount = 0;

    char* ptr = buffer + sizeof(DNSHeader);

    convertToDNSFormat(ptr, formattedName);
    ptr += strlen(ptr) + 1;

    DNSQuestion* question = (DNSQuestion*)ptr;
    question->qType = htons(isPTR ? 12 : 1); // 12 = PTR, 1 = A
    question->qClass = htons(1);
}

void readDNSName(char* response, char*& ptr, char* output, int recvSize) {
    char* orig_ptr = ptr; // Store the original pointer
    bool jumped = false;
    int offset, length = 0;

    set<int> visitedOffsets; // Track visited jumps

    while (*ptr) {
        if ((*ptr & 0xC0) == 0xC0) {  // Compression detected
            if (ptr + 1 >= response + recvSize) {  // Ensure full offset is readable
                printf("    ++ invalid record: truncated jump offset (e.g., 0xC0 and the packet ends)\n");
                exit(0);
            }

            offset = ntohs(*(unsigned short*)ptr) & 0x3FFF;

            // **Check for jump loop**
            if (visitedOffsets.count(offset)) {
                printf("    ++ invalid record: jump loop detected (offset %d revisited)\n", offset);
                exit(0);
            }
            visitedOffsets.insert(offset); // Mark offset as visited

            // **Check for jump into the fixed DNS header (first 12 bytes)**
            if (offset < sizeof(DNSHeader)) {
                printf("    ++ invalid record: jump into fixed DNS header (offset %d inside header)\n", offset);
                exit(0);
            }

            if (offset >= recvSize) {
                printf("    ++ invalid record: jump beyond packet boundary\n");
                exit(0);
            }

            if (!jumped) orig_ptr = ptr + 2;  // Save original pointer for return
            ptr = response + offset; // Follow the jump
            jumped = true;
        }
        else {
            int label_length = *ptr++;

            // Ensure valid label length (max 63 according to RFC 1035)
            if (label_length > 63) {
                printf("    ++ invalid record: label too long (label length %d exceeds 63)\n", label_length);
                exit(0);
            }

            // Ensure label is within packet bounds
            if (ptr + label_length >= response + recvSize) {
                printf("    ++ invalid record: truncated name (label length %d, but packet ends)\n", label_length);
                exit(0);
            }

            if (length + label_length > 255) {
                printf("    ++ invalid record: total DNS name exceeds 255 bytes (current length %d)\n", length + label_length);
                exit(0);
            }

            if (length) output[length++] = '.';
            memcpy(output + length, ptr, label_length);
            length += label_length;
            ptr += label_length;
        }
    }

    output[length] = '\0';

    // If we jumped, restore ptr to its original position
    if (jumped) {
        ptr = orig_ptr;
    }
    else {
        ptr++;  // Move past null byte when no compression
    }
}


void parseDNSResponse(char* response, int recvSize, unsigned short expectedTXID) {
    if (recvSize < sizeof(DNSHeader)) {
        printf("    ++ invalid reply: Packet smaller than fixed DNS header\n");
        return;
    }

    DNSHeader* resHeader = (DNSHeader*)response;
    unsigned short receivedTXID = ntohs(resHeader->TXID);

    if (receivedTXID != expectedTXID) {
        printf("    ++ invalid reply: TXID mismatch, sent 0x%.4X, received 0x%.4X\n", expectedTXID, receivedTXID);
        return;
    }

    unsigned short receivedflags = ntohs(resHeader->flags);
    unsigned short receivedQuestions = ntohs(resHeader->qdCount);
    unsigned short receivedAnswers = ntohs(resHeader->anCount);
    unsigned short receivedAuthority = ntohs(resHeader->nsCount);
    unsigned short receivedAdditional = ntohs(resHeader->arCount);

    printf("    TXID: 0x%04X, Flags: 0x%04X, Questions: %d, Answers: %d, Authority: %d, Additional: %d\n",
        receivedTXID, receivedflags, receivedQuestions, receivedAnswers, receivedAuthority, receivedAdditional);

    if ((receivedflags & 0xF) == 0) {
        printf("    succeeded with Rcode = 0\n");
    }
    else {
        printf("    failed with Rcode = %d\n", receivedflags & 0xF);
        return;
    }

    char* ptr = response + sizeof(DNSHeader);

    // **Print Questions**
    printf("    ------------ [questions] ------------\n");
    for (int i = 0; i < receivedQuestions; i++) {
        char domainName[256];
        readDNSName(response, ptr, domainName, recvSize);
        printf("    %s, type %d, class %d\n", domainName, ntohs(*(unsigned short*)ptr), ntohs(*(unsigned short*)(ptr + 2)));
        ptr += 4;
    }

    // **Parse Answers**
    if (receivedAnswers > 0) {
        int actualAnswers = 0;

        printf("    ------------ [answers] --------------\n");
        for (int i = 0; i < receivedAnswers; i++) {
            char domainName[256];

            if (ptr - response >= recvSize) {
                printf("    ++ invalid section: not enough records in Answers (Declared: %d answers but only %d found)\n", receivedAnswers, actualAnswers);
                return;
            }

            readDNSName(response, ptr, domainName, recvSize);

            if (ptr + sizeof(DNSAnswerHeader) > response + recvSize) {
                int remaining = response + recvSize - ptr;
                printf("    ++ invalid record: Truncated RR answer header in Answers (only %d bytes available, expected 10)\n", remaining);
                return;
            }

            DNSAnswerHeader* ansHeader = (DNSAnswerHeader*)ptr;
            ptr += sizeof(DNSAnswerHeader);

            unsigned short recordType = ntohs(ansHeader->type);
            unsigned int ttl = ntohl(ansHeader->TTL);
            unsigned short dataLen = ntohs(ansHeader->dataLength);


            if (ansHeader->_class != htons(1) && ansHeader->_class != htons(3)) {
                printf("    ++ invalid record: Unsupported DNS class (0x%04X)\n", ntohs(ansHeader->_class));
                return;
            }

            if (ttl > 31536000) {  // One year in seconds
                printf("    ++ invalid record: TTL too high (%u seconds exceeds 1 year)\n", ttl);
                return;
            }

            if (dataLen > recvSize - (ptr - response)) {
                printf("    ++ invalid record: RR value length stretches the answer beyond packet\n");
                return;
            }

            printf("    %s %s ", domainName, dnsTypeToString(recordType));

            if (recordType == DNS_A && dataLen == 4) {
                char ipStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, ptr, ipStr, INET_ADDRSTRLEN);
                printf("%s", ipStr);
            }
            else if (recordType == DNS_CNAME || recordType == DNS_PTR || recordType == DNS_NS) {
                char cname[256];
                readDNSName(response, ptr, cname, recvSize);
                printf("%s", cname);
            }
            else {
                printf("(Unsupported or malformed data)");
            }

            printf(" TTL = %u\n", ttl);

            if (!((*ptr & 0xC0) == 0xC0)) {
                ptr += dataLen;
            }
            actualAnswers++;
        }
    }

    // **Parse Authority Section**
    if (receivedAuthority > 0) {

        int actualAuthority = 0;

        if (ptr - response >= recvSize) {
            printf("    ++ invalid section: not enough records in Authority (Declared: %d Authorities but only %d found)\n", receivedAuthority, actualAuthority);
            return;
        }

        printf("    ------------ [authority] ------------\n");
        for (int i = 0; i < receivedAuthority; i++) {
            char domainName[256];
            readDNSName(response, ptr, domainName, recvSize);

            if (ptr + sizeof(DNSAnswerHeader) > response + recvSize) {
                int remaining = response + recvSize - ptr;
                printf("    ++ invalid record: Truncated RR answer header in Authority (only %d bytes available, expected 10)\n", remaining);
                return;
            }

            DNSAnswerHeader* authHeader = (DNSAnswerHeader*)ptr;
            ptr += sizeof(DNSAnswerHeader);

            unsigned short recordType = ntohs(authHeader->type);
            unsigned int ttl = ntohl(authHeader->TTL);
            unsigned short dataLen = ntohs(authHeader->dataLength);

            if (authHeader->_class != htons(1) && authHeader->_class != htons(3)) {
                printf("    ++ invalid record: Unsupported DNS class (0x%04X)\n", ntohs(authHeader->_class));
                return;
            }

            if (ttl > 31536000) {  // One year in seconds
                printf("    ++ invalid record: TTL too high (%u seconds exceeds 1 year)\n", ttl);
                return;
            }

            if (dataLen > recvSize - (ptr - response)) {
                printf("    ++ invalid record: RR value length stretches the answer beyond packet\n");
                return;
            }

            printf("    %s %s ", domainName, dnsTypeToString(recordType));

            char cname[256];
            readDNSName(response, ptr, cname, recvSize);
            printf("%s TTL = %u\n", cname, ttl);

            if (!((*ptr & 0xC0) == 0xC0)) {
                ptr += dataLen;
            }
            actualAuthority++;
        }
    }

    // **Parse Additional Section**
    if (receivedAdditional > 0) {

        int actualAdditional = 0;

        printf("    ------------ [additional] ------------\n");
        for (int i = 0; i < receivedAdditional; i++) {
            char domainName[256];

            if (ptr - response >= recvSize) {
                printf("    ++ invalid section: not enough records in Additional (Declared: %d Additionals but only %d found)\n", receivedAdditional, actualAdditional);
                return;
            }

            readDNSName(response, ptr, domainName, recvSize);

            if (ptr + sizeof(DNSAnswerHeader) > response + recvSize) {
                int remaining = response + recvSize - ptr;
                printf("    ++ invalid record: Truncated RR answer header in Additional (only %d bytes available, expected 10)\n", remaining);
                return;
            }

            DNSAnswerHeader* addHeader = (DNSAnswerHeader*)ptr;
            ptr += sizeof(DNSAnswerHeader);

            unsigned short recordType = ntohs(addHeader->type);
            unsigned int ttl = ntohl(addHeader->TTL);
            unsigned short dataLen = ntohs(addHeader->dataLength);

            if (addHeader->_class != htons(1) && addHeader->_class != htons(3)) {
                printf("    ++ invalid record: Unsupported DNS class (0x%04X)\n", ntohs(addHeader->_class));
                return;
            }

            if (ttl > 31536000) {  // One year in seconds
                printf("    ++ invalid record: TTL too high (%u seconds exceeds 1 year)\n", ttl);
                return;
            }


            if (dataLen > recvSize - (ptr - response)) {
                printf("    ++ invalid record: RR value length stretches the answer beyond packet\n");
                return;
            }

            printf("    %s %s ", domainName, dnsTypeToString(recordType));

            if (recordType == DNS_A && dataLen == 4) {
                char ipStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, ptr, ipStr, INET_ADDRSTRLEN);
                printf("%s", ipStr);
            }
            else if (recordType == DNS_CNAME || recordType == DNS_PTR || recordType == DNS_NS) {
                char cname[256];
                readDNSName(response, ptr, cname, recvSize);
                printf("%s", cname);
            }
            else {
                printf("(Unsupported or malformed data)");
            }

            printf(" TTL = %u\n", ttl);

            if (!((*ptr & 0xC0) == 0xC0)) {
                ptr += dataLen;
            }
            actualAdditional++;
        }
    }
}

void sendDNSQuery(const char* dnsServer, const char* hostname, bool isPTR) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cout << "WSAStartup failed with error " << WSAGetLastError() << endl;
        exit(1);
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        printf("Socket creation failed with error: %d\n", WSAGetLastError());
        WSACleanup();
        return;
    }

    struct sockaddr_in remote;
    remote.sin_family = AF_INET;
    remote.sin_port = htons(53);

    if (inet_pton(AF_INET, dnsServer, &remote.sin_addr) != 1) {
        printf("Invalid DNS server IP address.\n");
        closesocket(sock);
        WSACleanup();
        return;
    }

    char buffer[MAX_DNS_SIZE];
    char formattedName[256];

    if (isPTR) {
        convertIPtoPTR(formattedName, hostname);
    }
    else {
        memcpy(formattedName, hostname, strlen(hostname) + 1);
    }

    createDNSQuery(buffer, formattedName, isPTR);

    char* qname = buffer + sizeof(DNSHeader);
    int querySize = sizeof(DNSHeader) + strlen(qname) + 1 + sizeof(DNSQuestion);
    unsigned short expectedTXID = ntohs(*(unsigned short*)buffer);

    printf("Lookup : %s\nQuery  : %s, type %d, TXID 0x%.4X\nServer : %s\n",
        hostname, formattedName, isPTR ? 12 : 1, expectedTXID, dnsServer);
    printf("*************************************************************\n");

    struct sockaddr_in from;
    int fromLen = sizeof(from);
    char response[MAX_DNS_SIZE];

    // **Set up timeout: 10 seconds**
    struct timeval timeout;
    timeout.tv_sec = 10;  // 10 seconds timeout
    timeout.tv_usec = 0;

    fd_set fds;
    int attempts = 0;

    while (attempts < 3) {  // Maximum of 3 attempts
        printf("Attempt %d with %d bytes...", attempts, querySize);

        auto start_time = chrono::high_resolution_clock::now();
        sendto(sock, buffer, querySize, 0, (struct sockaddr*)&remote, sizeof(remote));

        FD_ZERO(&fds);
        FD_SET(sock, &fds);

        int selectResult = select(sock + 1, &fds, NULL, NULL, &timeout);

        auto end_time = chrono::high_resolution_clock::now();
        int duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();

        if (selectResult > 0) {
            // **Received a response**
            int recvSize = recvfrom(sock, response, MAX_DNS_SIZE, 0, (struct sockaddr*)&from, &fromLen);
            auto end_time = chrono::high_resolution_clock::now();
            int duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();
            if (recvSize > 0) {
                printf(" response in %d ms with %d bytes\n", duration, recvSize);
                parseDNSResponse(response, recvSize, expectedTXID);
                closesocket(sock);
                WSACleanup();
                return;
            }
            else {
                printf(" socket error %d\n", WSAGetLastError());
                return;
            }
        }
        else if (selectResult == 0) {
            // **Timeout occurred**
            printf("timeout in %d ms.\n", duration);
        }
        else {
            printf("Error in select(): %d\n", WSAGetLastError());
            break;
        }

        attempts++;
    }

    printf("Failed to receive a response after %d attempts. Exiting.\n", attempts);
    closesocket(sock);
    WSACleanup();
}

int main(int argc, char* argv[]) {
    srand(time(0));
    if (argc != 3) {
        printf("Usage: %s <hostname|IP> <dns-server-ip>\n", argv[0]);
        return 1;
    }

    struct in_addr addr;
    bool isPTR = (inet_pton(AF_INET, argv[1], &addr) == 1);
    sendDNSQuery(argv[2], argv[1], isPTR);
    return 0;
}
