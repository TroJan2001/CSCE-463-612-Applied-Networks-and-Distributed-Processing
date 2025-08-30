

#ifndef PCH_H
#define PCH_H

// add headers that you want to pre-compile here
#pragma comment(lib, "ws2_32.lib")
#include <iostream>
#include <cstring>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "SenderSocket.h"
#include <chrono>
#include <iomanip>
#include <algorithm>
#include "checksum.h"
#include <thread>


#endif //PCH_H
