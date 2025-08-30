#pragma once
#ifndef SENDERSOCKET_H
#define SENDERSOCKET_H

#pragma comment(lib, "Ws2_32.lib")

#define FORWARD_PATH 0
#define RETURN_PATH 1
#define MAGIC_PORT 22345
#define MAX_PKT_SIZE (1500 - 28) // Maximum UDP packet size
#define MAGIC_PROTOCOL 0x8311AA

// Status codes
#define STATUS_OK 0
#define ALREADY_CONNECTED 1
#define NOT_CONNECTED 2
#define INVALID_NAME 3
#define FAILED_SEND 4
#define TIMEOUT 5
#define FAILED_RECV 6

// Define network structures with 1-byte alignment
#pragma pack(1)

class Flags {
public:
    DWORD reserved : 5;
    DWORD SYN : 1;
    DWORD ACK : 1;
    DWORD FIN : 1;
    DWORD magic : 24;

    Flags() { memset(this, 0, sizeof(*this)); magic = MAGIC_PROTOCOL; }
};

class SenderDataHeader {
public:
    Flags flags;
    DWORD seq;
};

class LinkProperties {
public:
    float RTT;      // Round-trip time in seconds
    float speed;    // Bottleneck bandwidth in bits/sec
    float pLoss[2]; // Packet loss probability in each direction
    DWORD bufferSize;

    LinkProperties() { memset(this, 0, sizeof(*this)); }
};

class SenderSynHeader {
public:
    SenderDataHeader sdh;
    LinkProperties lp;
};

class ReceiverHeader {
public:
    Flags flags;
    DWORD recvWnd; // Receiver window for flow control (in pkts)
    DWORD ackSeq;  // Next expected sequence number
};

// Restore default struct alignment
#pragma pack()

class SenderSocket {
private:
    SOCKET sock;
    sockaddr_in serverAddr;
    bool isConnected;

public:
    SenderSocket();
    int Open(const char* targetHost, int senderWindow, LinkProperties* lp);
    int Close();
    ~SenderSocket();
};

#endif // SENDERSOCKET_H
