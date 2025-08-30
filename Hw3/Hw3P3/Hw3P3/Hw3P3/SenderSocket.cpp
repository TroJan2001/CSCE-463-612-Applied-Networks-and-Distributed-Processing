#include "pch.h"
#include "SenderSocket.h"

void SenderSocket::calculateStats(double prevRTT)
{
	double a = 0.125, b = 0.25;
	estRTT = (1 - a) * estRTT + a * prevRTT;
	devRTT = (1 - b) * devRTT + b * abs(prevRTT - estRTT);
	RTO = estRTT + 4 * max(devRTT, 0.010);
}

void startWinsock()
{
	WSADATA wsaData;
	int code = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (code != 0)
	{
		printf("WSAStartup failed with status %d\n", code);
		exit(1);
	}
}


DWORD WINAPI workerThread(LPVOID p)
{
	SenderSocket* ss = (SenderSocket*)p;
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

	HANDLE handles[] = {ss->eventQuit, ss->full, ss->socketReceiveReady};
	ss->expireTime = clock() + CLOCKS_PER_SEC * ss->RTO;
	while (true)
	{
		DWORD timeout;
		if (ss->nextToSend > ss->base) {
			clock_t now = clock();
			timeout = (ss->expireTime > now)
				? (1000 * (ss->expireTime - now) / CLOCKS_PER_SEC)
				: 0;
		}
		else {
			timeout = INFINITE;
		}

		// === Wait on events ===
		DWORD result = WaitForMultipleObjects(3, handles, FALSE, timeout);

		if (result == WAIT_OBJECT_0) // eventQuit
		{
			break;
		}
		else if (result == WAIT_OBJECT_0 + 1) // full semaphore
		{
				ss->effectiveWindow = min(ss->senderWindow, ss->receiverWindow);
				//printf("[debug] base=%d nextToSend=%d nextSeq=%d effectiveWindow=%d\n",
					//ss->base, ss->nextToSend, ss->nextSeq, ss->effectiveWindow);

				int index = ss->nextToSend % ss->senderWindow;
				Packet* pkt = &ss->pendingPackets[index];
				SenderDataHeader* sdh = (SenderDataHeader*)pkt->pkt;
				// Set flags
				sdh->flags.reserved = 0;
				sdh->flags.SYN = 0;
				sdh->flags.ACK = 0;
				sdh->flags.FIN = 0;
				sdh->flags.magic = MAGIC_PROTOCOL;
				pkt->txTime = clock();
				int totalSize = sizeof(SenderDataHeader) + pkt->size;

				if (sendto(ss->sock, pkt->pkt, totalSize, 0, (sockaddr*)&ss->remote, sizeof(ss->remote)) == SOCKET_ERROR)
				{
					printf("Worker sendto failed with %d\n", WSAGetLastError());
					ss->totalRets++;
					SetEvent(ss->eventQuit);
				}
				else
				{
					//printf("[worker] Sent seq %d size %d\n", sdh->seq, pkt->size);
				}
				ss->nextToSend++;
				if (ss->base == ss->nextToSend) {
					ss->expireTime = clock() + CLOCKS_PER_SEC * ss->RTO;
				}
		}
		else if (result == WAIT_OBJECT_0 + 2) // socket ready to read
		{
				ReceiverHeader rh;
				sockaddr_in from;
				int len = sizeof(from);
				int bytes = recvfrom(ss->sock, (char*)&rh, sizeof(rh), 0, (sockaddr*)&from, &len);

				if (bytes != SOCKET_ERROR)
				{
					if (rh.ackSeq > ss->base)
					{
						//printf("[worker] Received ackSeq %d (base = %d)\n", rh.ackSeq, ss->base);
						int newlyAcked = rh.ackSeq - ss->base;
						if (ss->retxForBase == 0)
						{
							double rtt = (clock() - ss->pendingPackets[(rh.ackSeq - 1) % ss->senderWindow].txTime) / (double)CLOCKS_PER_SEC;
							ss->calculateStats(rtt);
							//printf("[worker] RTO changed to %.3f\n", ss->RTO);
						}
						ss->ACKbytes += newlyAcked * MAX_PKT_SIZE;
						ss->effectiveWindow = min(ss->senderWindow, rh.recvWnd);
						ss->receiverWindow = rh.recvWnd;
						ss->base = rh.ackSeq;
						ss->retxForBase = 0;
						int slotsToRelease = ss->base + ss->effectiveWindow - ss->lastReleased;
						//printf("[worker] releasing %d base %d effectiveWindow %d lastReleased %d\n", slotsToRelease, ss->base, ss->effectiveWindow, ss->lastReleased);
						ss->lastReleased += slotsToRelease;
						ss->dupACK = 0;
						ss->expireTime = clock() + CLOCKS_PER_SEC * ss->RTO;
						ReleaseSemaphore(ss->empty, slotsToRelease, NULL);
					}
					else if (rh.ackSeq == ss->base)
					{
						ss->dupACK++;//edit this
						if (ss->dupACK == 3)
						{
							int index = ss->base % ss->senderWindow;
							Packet* pkt = &ss->pendingPackets[index];
							SenderDataHeader* sdh = (SenderDataHeader*)pkt->pkt;
							int totalSize = sizeof(SenderDataHeader) + pkt->size;

							ss->retxForBase++;

							if (sendto(ss->sock, pkt->pkt, totalSize, 0, (sockaddr*)&ss->remote, sizeof(ss->remote)) == SOCKET_ERROR)
							{
								printf("[Worker] dupack retransmit failed with %d\n", WSAGetLastError());
								continue;
							}
							//printf("[worker] Triple dupACK -> Fast retransmit seq %d\n", sdh->seq);
							pkt->txTime = clock();
							ss->totalRets++;
							ss->totalFastRetransmit++;
							ss->expireTime = pkt->txTime + CLOCKS_PER_SEC * ss->RTO;

						}
					}
				}
				else {
					printf("Worker recvfrom failed with %d\n", WSAGetLastError());
					SetEvent(ss->eventQuit);
				}
		}
		else if (result == WAIT_TIMEOUT)
		{
			if (ss->nextToSend > ss->base)
			{
				int index = ss->base % ss->senderWindow;
				Packet* pkt = &ss->pendingPackets[index];
				SenderDataHeader* sdh = (SenderDataHeader*)pkt->pkt;
				int totalSize = sizeof(SenderDataHeader) + pkt->size;

				ss->retxForBase++;

				if (ss->retxForBase > MAX_ATTEMPTS)
				{
					//printf("[worker] Too many retx for base %d ? terminating\n", ss->base);
					SetEvent(ss->eventQuit);
					break;
				}

				if (sendto(ss->sock, pkt->pkt, totalSize, 0, (sockaddr*)&ss->remote, sizeof(ss->remote)) == SOCKET_ERROR)
				{
					printf("Worker retransmit failed with %d\n", WSAGetLastError());
					SetEvent(ss->eventQuit);
				}
				else
				{
					//printf("[worker] Timeout: Retransmitted seq %d timeout = %d\n", sdh->seq, timeout);
					pkt->txTime = clock();
					ss->expireTime = clock() + CLOCKS_PER_SEC * ss->RTO;
					ss->totalRets++;
					ss->totalTimeouts++;
				}
			}

		}
		else
		{
			printf("Worker: unexpected wait result: %lu\n", result);
			SetEvent(ss->eventQuit);
			break;
		}
	}

	return 0;
}



int SenderSocket::Open(char* targetHost, int port, int senderWindow, LinkProperties* lp)
{
	if (this->isOpen) // check if socket is already open
	{
		return ALREADY_CONNECTED;
	}

	// set up the socket
	startWinsock();
	SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET)
	{
		closesocket(sock);
		WSACleanup();
		exit(1);
	}
	int kernelBuffer = 20 * 1024 * 1024; // 20 MB
	setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&kernelBuffer, sizeof(kernelBuffer));
	setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&kernelBuffer, sizeof(kernelBuffer));

	memset(&(this->local), 0, sizeof(this->local));
	this->local.sin_family = AF_INET;
	this->local.sin_addr.s_addr = INADDR_ANY;
	this->local.sin_port = htons(0);
	if (bind(sock, (struct sockaddr*)&(this->local), sizeof(this->local)) == SOCKET_ERROR)
	{
		closesocket(sock);
		WSACleanup();
		exit(1);
	}
	// set up remote socket
	memset(&(this->remote), 0, sizeof(this->remote));
	this->remote.sin_family = AF_INET;
	this->remote.sin_port = htons(port);

	addrinfo hints = {}, * res = nullptr;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;

	char portStr[6];
	sprintf_s(portStr, "%d", port);

	if (getaddrinfo(targetHost, portStr, &hints, &res) != 0) {
		printf("target %s is invalid\n", targetHost);
		closesocket(sock);
		WSACleanup();
		return INVALID_NAME;
	}

	memcpy(&(this->remote), res->ai_addr, sizeof(this->remote));
	freeaddrinfo(res);

	// Generate SYN packet to send
	SenderSynHeader* synPacket = new SenderSynHeader();
	synPacket->lp.RTT = lp->RTT;
	synPacket->lp.speed = lp->speed;
	synPacket->lp.pLoss[0] = lp->pLoss[0];
	synPacket->lp.pLoss[1] = lp->pLoss[1];
	synPacket->lp.bufferSize = 3 + senderWindow;
	synPacket->sdh.flags.reserved = 0;
	synPacket->sdh.flags.SYN = 1;
	synPacket->sdh.flags.ACK = 0;
	synPacket->sdh.flags.FIN = 0;
	synPacket->sdh.flags.magic = MAGIC_PROTOCOL;
	synPacket->sdh.seq = 0;


	int currAttempt = 0;

	double rto = max(1, 2 * lp->RTT);
	clock_t start;
	clock_t fStart = this->timeCreated;

	double rtt;
	timeval tp;
	fd_set fd;

	while (currAttempt < SYN_MAX_ATTEMPTS)
	{
		start = clock();
		if (sendto(sock, (char*)synPacket, sizeof(SenderSynHeader), 0, (sockaddr*)&this->remote, sizeof(this->remote)) == SOCKET_ERROR)
		{
			printf("failed sendto with %d\n", WSAGetLastError());
			closesocket(sock);
			WSACleanup();
			return FAILED_SEND;
		}

		tp.tv_sec = floor(rto);
		tp.tv_usec = 1e6 * (rto - tp.tv_sec);
		FD_ZERO(&fd);
		FD_SET(sock, &fd);

		int sel = select(0, &fd, NULL, NULL, &tp);
		if (sel == SOCKET_ERROR)
		{
			printf("error %ld in select()\n", WSAGetLastError());
			closesocket(sock);
			WSACleanup();
			exit(1);
		}
		else if (sel == 0)
		{
			if (currAttempt == 2)
			{
				closesocket(sock);
				WSACleanup();
				return TIMEOUT;
			}

			currAttempt += 1;
			totalRets += 1;
			totalTimeouts += 1;
			continue;
		}
		else if (sel > 0)
		{
			// attempt recv
			ReceiverHeader rh;
			struct sockaddr_in response;
			int size = sizeof(response);

			int bytes = recvfrom(sock, (char*)&rh, sizeof(rh), 0, (sockaddr*)&response, &size);

			if (bytes == SOCKET_ERROR)
			{
				printf("failed recvfrom with %ld\n", WSAGetLastError());
				closesocket(sock);
				WSACleanup();
				return FAILED_RECV;
			}
			if (rh.flags.SYN && rh.flags.ACK && rh.flags.magic == MAGIC_PROTOCOL)
			{
				rtt = (clock() - start) / (double)CLOCKS_PER_SEC;
				estRTT = rtt;
				RTO = 3 * estRTT;
				this->receiverWindow = rh.recvWnd;
				this->senderWindow = senderWindow;
				int initialCredits = min(senderWindow, receiverWindow);
				lastReleased = initialCredits;
				empty = CreateSemaphore(NULL, initialCredits, senderWindow, NULL);
				full = CreateSemaphore(NULL, 0, senderWindow, NULL);
				eventQuit = CreateEvent(NULL, TRUE, FALSE, NULL);
				socketReceiveReady = CreateEvent(NULL, FALSE, FALSE, NULL);
				break;
			}
		}
	}
	this->isOpen = true;
	this->sock = sock;
	this->pendingPackets = new Packet[senderWindow];
	
	WSAEventSelect(sock, socketReceiveReady, FD_READ);
	nextToSend = 0;
	workerHandle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)workerThread, this, 0, NULL);

	return STATUS_OK;
}

int SenderSocket::Close(LinkProperties* lp, int senderWindow, char* buffer)
{
	while (base < nextSeq)
	{
		//printf("Waiting to ACKs\n");
		DWORD res = WaitForSingleObject(eventQuit, 10); // non-blocking check
		if (res == WAIT_OBJECT_0)
		{
			printf("Close: giving up early, quit event was signaled\n");
			return TIMEOUT;
		}
		Sleep(1); // avoid busy waiting
	}
	
	int lastIdx = (nextSeq - 1) % senderWindow;
	int actualPayload = pendingPackets[lastIdx].size;
	int fullPayload = MAX_PKT_SIZE - sizeof(SenderDataHeader);
	ACKbytes -= (fullPayload - actualPayload);

	timeval tp;
	fd_set fd;
	clock_t start;

	// Generate FIN packet to send
	SenderDataHeader* finPacket = new SenderDataHeader();
	finPacket->flags.reserved = 0;
	finPacket->flags.SYN = 0;
	finPacket->flags.ACK = 0;
	finPacket->flags.FIN = 1;
	finPacket->flags.magic = MAGIC_PROTOCOL;
	finPacket->seq = base;

	// Send FIN and wait for FIN-ACK
	int currAttempt = 0;
	double fElapsed;
	clock_t fStart = clock();

	while (currAttempt < MAX_ATTEMPTS)
	{
		start = clock();
		if (sendto(sock, (char*)finPacket, sizeof(SenderDataHeader), 0, (sockaddr*)&remote, sizeof(remote)) == SOCKET_ERROR)
		{
			printf("Close: sendto failed with %d\n", WSAGetLastError());
			closesocket(sock);
			WSACleanup();
			return FAILED_SEND;
		}

		tp.tv_sec = floor(RTO);
		tp.tv_usec = (long)(1e6 * (RTO - tp.tv_sec));
		FD_ZERO(&fd);
		FD_SET(sock, &fd);

		int sel = select(0, &fd, NULL, NULL, &tp);
		if (sel == SOCKET_ERROR)
		{
			printf("Close: select() failed with %ld\n", WSAGetLastError());
			closesocket(sock);
			WSACleanup();
			return FAILED_RECV;
		}
		else if (sel == 0)
		{
			currAttempt++;
			totalTimeouts++;
			totalRets++;
			continue;
		}
		else
		{
			ReceiverHeader rh;
			sockaddr_in response;
			int size = sizeof(response);
			int bytes = recvfrom(sock, (char*)&rh, sizeof(rh), 0, (sockaddr*)&response, &size);
			if (bytes == SOCKET_ERROR)
			{
				int err = WSAGetLastError();
				if (err == WSAEWOULDBLOCK) {
					continue;
				}
				printf("Close: recvfrom failed with %ld\n", err);
				closesocket(sock);
				WSACleanup();
				return FAILED_RECV;
			}

			if (rh.flags.FIN && rh.flags.ACK && rh.flags.magic == MAGIC_PROTOCOL)
			{
				fElapsed = (clock() - timeCreated) / (double)CLOCKS_PER_SEC;
				printf("[ %.2f] <-- FIN-ACK %d window %08X\n", fElapsed, rh.ackSeq, rh.recvWnd);
				break;
			}
		}
	}

	this->isOpen = false;
	return STATUS_OK;
}

int SenderSocket::Send(char* buffer, int bytes)
{
	HANDLE arr[] = {eventQuit, empty};
	DWORD result = WaitForMultipleObjects(2, arr, FALSE, INFINITE);

	if (result == WAIT_OBJECT_0) // eventQuit signaled
		return TIMEOUT;

	// We got an empty slot
	int slot = nextSeq % senderWindow;
	this->effectiveWindow = min(senderWindow, receiverWindow);
	//printf("[Send] nextSeq %d and effective window Size %d\n", nextSeq, this->effectiveWindow);
	Packet* p = &pendingPackets[slot];
	//printf("[Send] Queuing seq %d in slot %d\n", nextSeq, nextSeq % senderWindow);

	p->type = 0;
	p->size = bytes;
	SenderDataHeader* sdh = (SenderDataHeader*)p->pkt;
	sdh->seq = nextSeq;
	memcpy(sdh + 1, buffer, bytes);
	nextSeq++;

	ReleaseSemaphore(full, 1, NULL);

	return STATUS_OK;
}

SenderSocket::~SenderSocket() {
	// Gracefully stop the worker thread if still alive
	if (workerHandle != NULL) {
		DWORD exitCode;
		GetExitCodeThread(workerHandle, &exitCode);
		if (exitCode == STILL_ACTIVE) {
			SetEvent(eventQuit);  // signal quit
			WaitForSingleObject(workerHandle, INFINITE);
		}
		CloseHandle(workerHandle);
		workerHandle = NULL;
	}

	// Clean up any thread-related handles
	if (socketReceiveReady) CloseHandle(socketReceiveReady);
	if (empty) CloseHandle(empty);
	if (full) CloseHandle(full);
	if (eventQuit) CloseHandle(eventQuit);

	// Clean up packet buffer
	if (pendingPackets) {
		delete[] pendingPackets;
		pendingPackets = nullptr;
	}

	// Close socket
	if (sock != INVALID_SOCKET) {
		closesocket(sock);
		sock = INVALID_SOCKET;
	}

	// Final cleanup
	WSACleanup();
}
