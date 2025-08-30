#include "pch.h"

using namespace std;

double GetTime() {
    static auto start = chrono::high_resolution_clock::now();
    auto now = chrono::high_resolution_clock::now();
    chrono::duration<double> elapsed = now - start;
    return elapsed.count();
}

SenderSocket::SenderSocket() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WSAStartup failed\n";
        exit(1);
    }
    sock = INVALID_SOCKET;
    isConnected = false;
    memset(&serverAddr, 0, sizeof(serverAddr));
}

int SenderSocket::Open(const char* targetHost, int senderWindow, LinkProperties* lp) {
    if (isConnected) return ALREADY_CONNECTED;

    // Set buffer size
    lp->bufferSize = senderWindow + 3;

    // Create socket
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        cerr << "socket failed with " << WSAGetLastError() << "\n";
        return FAILED_SEND;
    }

    sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = 0;
    if (bind(sock, (sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
        cerr << "bind failed with " << WSAGetLastError() << "\n";
        closesocket(sock);
        return FAILED_SEND;
    }

    // Resolve target
    addrinfo hints = { 0 }, * res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(targetHost, nullptr, &hints, &res) != 0) {
        cerr << "[" << fixed << setprecision(3) << GetTime() << "] --> target " << targetHost << " is invalid\n";
        return INVALID_NAME;
    }

    sockaddr_in* addr = (sockaddr_in*)res->ai_addr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(MAGIC_PORT);
    serverAddr.sin_addr = addr->sin_addr;
    freeaddrinfo(res);

    // Construct SYN packet
    SenderSynHeader synPacket = {};
    synPacket.sdh.flags.SYN = 1;
    synPacket.sdh.seq = 0;
    synPacket.lp = *lp;

    sockaddr_in fromAddr;
    int fromLen = sizeof(fromAddr);
    ReceiverHeader recvHeader = {};
    const int MAX_ATTEMPTS = 3;
    double RTO = 1.0;

    for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
        double t0 = GetTime();
        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(serverAddr.sin_addr), ipStr, INET_ADDRSTRLEN);

        cout << "[" << fixed << setprecision(3) << t0 << "] --> SYN 0 (attempt " << attempt << " of 3, RTO " << RTO << ") to "
            << ipStr << "\n";


        if (sendto(sock, (char*)&synPacket, sizeof(synPacket), 0, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            cerr << "[" << fixed << setprecision(3) << GetTime() << "]" << " <-- failed sendto with " << WSAGetLastError() << "\n";
            return FAILED_SEND;
        }

        // Set up fd_set for select()
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(sock, &readSet);

        // Timeout setup
        timeval timeout = { (int)RTO, (int)((RTO - (int)RTO) * 1e6) };

        // Wait for data to be ready
        int ready = select(0, &readSet, nullptr, nullptr, &timeout);
        if (ready > 0) {
            int bytes = recvfrom(sock, (char*)&recvHeader, sizeof(recvHeader), 0, (sockaddr*)&fromAddr, &fromLen);
            if (bytes != SOCKET_ERROR) {
                if (recvHeader.flags.SYN && recvHeader.flags.ACK) {
                    double rtt = GetTime() - t0;
                    RTO = 3 * rtt;
                    cout << "[" << fixed << setprecision(3) << GetTime() << "] <-- SYN-ACK 0 window " << recvHeader.recvWnd
                        << "; setting initial RTO to " << RTO << "\n";
                    isConnected = true;
                    return STATUS_OK;
                }
            }
            else {
                cerr << "[" << fixed << setprecision(3) << GetTime() << "] <-- failed recvfrom with " << WSAGetLastError() << "\n";
                return FAILED_RECV;
            }
        }
        else if (ready == 0) {
            continue;
        }
        else {
            cerr << "[" << fixed << setprecision(3) << GetTime() << "] <-- failed select with " << WSAGetLastError() << "\n";
        }
    }
    return TIMEOUT;
}

int SenderSocket::Close() {
    if (!isConnected) return NOT_CONNECTED;

    SenderDataHeader finPacket = {};
    finPacket.flags.FIN = 1;
    finPacket.seq = 0;

    sockaddr_in fromAddr;
    int fromLen = sizeof(fromAddr);
    ReceiverHeader recvHeader;
    const int MAX_ATTEMPTS = 5;
    double RTO = 0.663;

    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt) {
        cout << "[ " << fixed << setprecision(3) << GetTime() << " ] --> FIN 0 (attempt " << attempt << " of 5, RTO " << RTO << ")\n";

        if (sendto(sock, (char*)&finPacket, sizeof(finPacket), 0, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            cerr << "sendto failed with " << WSAGetLastError() << "\n";
            return FAILED_SEND;
        }

        // Setup select()
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(sock, &readSet);
        timeval timeout = { (int)RTO, (int)((RTO - (int)RTO) * 1e6) };

        int ready = select(0, &readSet, nullptr, nullptr, &timeout);
        if (ready > 0) {
            int bytes = recvfrom(sock, (char*)&recvHeader, sizeof(recvHeader), 0, (sockaddr*)&fromAddr, &fromLen);
            if (bytes != SOCKET_ERROR) {
                if (recvHeader.flags.FIN && recvHeader.flags.ACK) {
                    cout << "[ " << fixed << setprecision(3) << GetTime() << " ] <-- FIN-ACK 0 window " << recvHeader.recvWnd << "\n";
                    isConnected = false;
                    return STATUS_OK;
                }
            }
            else {
                return FAILED_RECV;
            }
        }
        else if (ready == 0) {
            continue;
        }
        else {
            cerr << "[ " << fixed << setprecision(3) << GetTime() << " ] <-- select() failed with " << WSAGetLastError() << "\n";
        }

    }
    return TIMEOUT;
}

SenderSocket::~SenderSocket() {
    if (sock != INVALID_SOCKET) closesocket(sock);
    WSACleanup();
}
