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
	{;
		closesocket(sock);
		WSACleanup();
		exit(1);
	}

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
	
	double rto = max(1, 2*lp->RTT);
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

			currAttempt += 1; // next attempt
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
				break;
			}
		}
	}
	this->isOpen = true;
	this->sock = sock;
	return STATUS_OK;
}

int SenderSocket::Close(LinkProperties* lp, int senderWindow, char* buffer)
{
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
				printf("Close: recvfrom failed with %ld\n", WSAGetLastError());
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

	int currAttempt = 0;
	while (currAttempt < MAX_ATTEMPTS)
	{
		Packet* pkt = new Packet();
		memset(pkt->data, 0, MAX_PKT_SIZE);
		pkt->sdh.seq = base;
		memcpy(pkt->data, buffer, bytes);

		// Send data packet 
		double fElapsed;
		double rto = RTO;

		// Receive data ACK
		double rtt;
		timeval tp;
		fd_set fd;
		clock_t start = clock();
		if (sendto(sock, (char*)pkt, sizeof(SenderDataHeader) + bytes, 0, (sockaddr*)&remote, sizeof(remote)) == SOCKET_ERROR)
		{
			printf("failed sendto with %d\n", WSAGetLastError());
			closesocket(sock);
			WSACleanup();
			return FAILED_SEND;
		}

		// reset select each time
		tp.tv_sec = floor(rto);
		tp.tv_usec = 1e6 * (rto - tp.tv_sec);
		FD_ZERO(&fd);       // clear the set
		FD_SET(sock, &fd);  // add socket to the set

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
			if (currAttempt == 4)
			{
				closesocket(sock);
				WSACleanup();
				return TIMEOUT;
			}
			totalTimeouts += 1;
			totalRets += 1;
			currAttempt += 1;
			continue;
		}
		else if (sel > 0)
		{
			// attempt to receive
			ReceiverHeader rh;
			struct sockaddr_in response;
			int bytes;
			int size = sizeof(response);

			if ((bytes = recvfrom(sock, (char*)&rh, sizeof(ReceiverHeader), 0, (struct sockaddr*)&response, &size)) == SOCKET_ERROR)
			{
				fElapsed = (clock() - timeCreated) / (double)CLOCKS_PER_SEC;
				closesocket(sock);
				WSACleanup();
				return FAILED_RECV;
			}
			
			if (rh.ackSeq == base+1)
			{
				rtt = (clock() - start) / (double)CLOCKS_PER_SEC;
				calculateStats(rtt);
				windowSize = min(windowSize, rh.recvWnd);
				base++;
				nextSeq++;
				ACKbytes += MAX_PKT_SIZE;
				break;
			}
		}
	}
	return STATUS_OK;
}