// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

#define DNS_PORT 53
#define MAX_DNS_SIZE 512
#define MAX_ATTEMPTS 3

/* DNS query types */
#define DNS_A  1
#define DNS_NS  2
#define DNS_CNAME  5
#define DNS_PTR  12
#define DNS_HINFO  13
#define DNS_MX  15
#define DNS_AXFR  252
#define DNS_ANY 255

// add headers that you want to pre-compile here

#endif //PCH_H
#pragma comment(lib, "ws2_32.lib")
#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <set>



