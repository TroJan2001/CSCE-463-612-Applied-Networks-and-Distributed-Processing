#include "pch.h"
using namespace std;

// Convert IP address (string) into DNS PTR query format
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

// Convert a regular hostname or PTR query into DNS wire format
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

// Create a basic DNS query packet into buffer
void createDNSQuery(char* buffer, unsigned short txid, const char* ipstr) {

    DNSHeader* header = (DNSHeader*)buffer;
    memset(buffer, 0, MAX_DNS_SIZE);

    header->TXID = htons(txid);
    header->flags = htons(0x0100); // Standard Query
    header->qdCount = htons(1);    // 1 Question

    char* ptr = buffer + sizeof(DNSHeader);

    // format: 4.3.2.1.in-addr.arpa for PTR
    char ptrQuery[256];
    convertIPtoPTR(ptrQuery, ipstr);

    convertToDNSFormat(ptr, ptrQuery);
    ptr += strlen(ptr) + 1;

    DNSQuestion* question = (DNSQuestion*)ptr;
    question->qType = htons(DNS_PTR);  // PTR record
    question->qClass = htons(1);        // IN class
}

// Parse DNS reply and extract resolved name
bool parseDNSReply(char* response, int recvSize, unsigned short expectedTXID, char* resolved_name) {

    if (recvSize < sizeof(DNSHeader)) {
        return false;
    }

    DNSHeader* resHeader = (DNSHeader*)response;
    if (ntohs(resHeader->TXID) != expectedTXID) {
        return false;
    }

    if ((ntohs(resHeader->flags) & 0xF) != 0) {
        return false; // Rcode != 0 means error
    }

    unsigned short ancount = ntohs(resHeader->anCount);
    if (ancount == 0) {
        return false;
    }

    char* ptr = response + sizeof(DNSHeader);

    // Skip Question Section
    while (*ptr) {
        ptr++;
    }
    ptr += 1 + 4; // Null + QTYPE + QCLASS

    for (int i = 0; i < ancount; i++) {
        char domainName[256];
        readDNSName(response, ptr, domainName, recvSize);

        DNSAnswerHeader* ansHeader = (DNSAnswerHeader*)ptr;
        ptr += sizeof(DNSAnswerHeader);

        unsigned short recordType = ntohs(ansHeader->type);
        unsigned short dataLen = ntohs(ansHeader->dataLength);

        if (recordType == DNS_PTR) {
            readDNSName(response, ptr, resolved_name, recvSize);

            return true;
        }
        else {
            ptr += dataLen;
        }
    }
    return false;
}

// Read compressed DNS names from packet
void readDNSName(char* response, char*& ptr, char* output, int recvSize) {
    int length = 0;
    bool jumped = false;
    char* original_ptr = ptr;
    set<int> jumps;

    while (*ptr) {
        if ((*ptr & 0xC0) == 0xC0) {
            int offset = ntohs(*(unsigned short*)ptr) & 0x3FFF;
            if (offset >= recvSize) {
                break;
            }
            if (jumps.count(offset)) {
                break;
            }
            jumps.insert(offset);
            ptr = response + offset;
            jumped = true;
        }
        else {
            int label_len = *ptr++;
            if (label_len == 0 || label_len > 63) break;
            if (length) output[length++] = '.';
            memcpy(output + length, ptr, label_len);
            length += label_len;
            ptr += label_len;
        }
    }
    output[length] = '\0';

    if (!jumped) {
        ptr++;
    }
    else {
        ptr = original_ptr + 2;
    }
}
