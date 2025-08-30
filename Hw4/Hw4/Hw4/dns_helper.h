#ifndef DNS_HELPER_H
#define DNS_HELPER_H

#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <set>

#define DNS_PORT 53
#define MAX_DNS_SIZE 512

// DNS Query Types
#define DNS_A      1
#define DNS_NS     2
#define DNS_CNAME  5
#define DNS_PTR    12
#define DNS_HINFO  13
#define DNS_MX     15
#define DNS_AXFR   252
#define DNS_ANY    255

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

// Function declarations
void convertIPtoPTR(char* ptrQuery, const char* ipAddr);
void convertToDNSFormat(char* dns, const char* hostname);
void createDNSQuery(char* buffer, unsigned short txid, const char* ipstr);
bool parseDNSReply(char* response, int recvSize, unsigned short expectedTXID, char* resolved_name);
void readDNSName(char* response, char*& ptr, char* output, int recvSize);

#endif // DNS_HELPER_H
