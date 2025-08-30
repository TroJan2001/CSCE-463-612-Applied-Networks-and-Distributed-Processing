#pragma once

#define SYN_MAX_ATTEMPTS	3	// max number of attempts for SYN packets
#define MAX_ATTEMPTS		50	// max number of attempts for all other packets

#define MAGIC_PORT 22345		// receiver listens on this port
#define MAX_PKT_SIZE (1500-28)	// max UDP packet size accepted by receiver

// possible status codes from ss.Open, ss.Send, ss.Close
#define STATUS_OK			0	// no error
#define ALREADY_CONNECTED	1	// 2nd call to ss.Open() without closing connection
#define NOT_CONNECTED		2	// call to ss.Send()/Close() wihtout ss.Open()
#define INVALID_NAME		3	// ss.Open() with targetHost that has no DNS entry
#define FAILED_SEND			4	// sendto() failed in kernel
#define TIMEOUT				5	// timeout after all retx attempts are exhausted
#define FAILED_RECV			6	// recvfrom() failed in kernel

// SYN packets format
#define FORWARD_PATH		0
#define RETURN_PATH			1

// Flags Header
#define MAGIC_PROTOCOL	0x8311AA

#pragma pack(push, 1)
class LinkProperties
{
public:
	// transfer parameters
	float RTT;			// propagation RTT (in sec)
	float speed;		// bottleneck bandwidth (in bits/sec)
	float pLoss[2];		// probability of loss in each direction
	DWORD bufferSize;	// buffer size of emulated routers (in packets)
	LinkProperties() { memset(this, 0, sizeof(*this)); }
};

class Flags
{
public:
	DWORD reserved : 5;
	DWORD SYN : 1;
	DWORD ACK : 1;
	DWORD FIN : 1;
	DWORD magic : 24;
	Flags() { memset(this, 0, sizeof(*this)); magic = MAGIC_PROTOCOL; }
};

class SenderDataHeader
{
public:
	Flags flags;
	DWORD seq = 0;	// must begin with 0
};

class SenderSynHeader
{
public:
	SenderDataHeader sdh;
	LinkProperties lp;
};

class ReceiverHeader
{
public:
	Flags flags;
	DWORD recvWnd = 1; // receiver window for flow control (in packets)
	DWORD ackSeq = 0; // ack value = next expected sequence
};

class Packet
{
public:
	int type;
	int size;
	clock_t txTime;
	char pkt[MAX_PKT_SIZE];
};

#pragma pack(pop)

class SenderSocket
{

public:

	SenderSocket() : sock(INVALID_SOCKET) {
		memset(&remote, 0, sizeof(remote));
		memset(&local, 0, sizeof(local));
	}

	bool isOpen = false; // keep track of whether socket is open/closed
	float estRTT = 0, devRTT = 0, ACKbytes = 0;
	float RTO = 1.0;
	int base = 0, nextSeq = 0, totalTimeouts = 0, totalRets = 0, retxForBase = 0;;
	int lastReleased = 0, nextToSend = 0, dupACK = 0;
	int senderWindow = 0, receiverWindow = 0, effectiveWindow=0;
	int totalFastRetransmit = 0;


	Packet* pendingPackets = nullptr;
	HANDLE empty = NULL;
	HANDLE full = NULL;
	HANDLE eventQuit = NULL;
	HANDLE workerHandle = NULL;
	HANDLE socketReceiveReady = NULL;
	HANDLE sendThreadHandle = NULL;
	HANDLE recvThreadHandle = NULL;
	HANDLE timeoutThreadHandle = NULL;
	HANDLE timeout = NULL;

	SOCKET sock;
	struct sockaddr_in remote;
	struct sockaddr_in local;

	clock_t timeCreated = clock(); // saved time when constructor was called
	clock_t expireTime = clock();


	bool finAckReceived = false;
	ReceiverHeader finAckHeader = {};

	int Open(char* targetHost, int port, int senderWindow, LinkProperties* lp);
	int Close(LinkProperties* lp, int senderWindow, char* buffer);
	int Send(char* buffer, int bytes);
	void calculateStats(double prevRTT); // helper function calculates stat values to print
	~SenderSocket();
};

struct Parameters
{
	SenderSocket* ss;
	bool stop;
};