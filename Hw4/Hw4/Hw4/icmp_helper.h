#ifndef ICMP_HELPER_H
#define ICMP_HELPER_H

#include "pch.h"

// Constants
#define IP_HDR_SIZE 20
#define ICMP_HDR_SIZE 8
#define MAX_SIZE 65200
#define MAX_ICMP_SIZE (MAX_SIZE + ICMP_HDR_SIZE)
#define MAX_REPLY_SIZE (IP_HDR_SIZE + ICMP_HDR_SIZE + MAX_ICMP_SIZE)

// ICMP Types
#define ICMP_ECHO_REPLY 0
#define ICMP_DEST_UNREACH 3
#define ICMP_TTL_EXPIRED 11
#define ICMP_ECHO_REQUEST 8

// IP Header
#pragma pack(push, 1)
typedef struct {
    u_char h_len : 4;
    u_char version : 4;
    u_char tos;
    u_short len;
    u_short ident;
    u_short flags;
    u_char ttl;
    u_char proto;
    u_short checksum;
    u_long source_ip;
    u_long dest_ip;
} IPHeader;

// ICMP Header
typedef struct {
    u_char type;
    u_char code;
    u_short checksum;
    u_short id;
    u_short seq;
} ICMPHeader;

struct HopResult {
    bool replied;              // Did this TTL reply yet?
    int ttl;                   // TTL value
    char ipstr[INET_ADDRSTRLEN]; // IP address as string
    double rtt_ms;             // RTT in milliseconds
    int attempts;              // How many times we tried
    char resolved_name[256];   // DNS name (resolved later)
    char error_string[100];    // If destination unreachable
    LARGE_INTEGER send_time;   // ICMP send time

    // New for Homework 4:
    bool dns_sent;             // Was DNS query sent yet?
    bool dns_replied;          // Was DNS reply received?
    unsigned short dns_txid;   // DNS TXID for matching
    LARGE_INTEGER dns_send_time; // DNS send timestamp
};


#pragma pack(pop)



// Function declarations
u_short ip_checksum(u_short* buffer, int size);

#endif