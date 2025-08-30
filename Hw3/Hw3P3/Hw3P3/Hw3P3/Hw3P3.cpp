//Semester: Spring/2025
//Course: CSCE 612
//Hw3P3

#include "pch.h"
#pragma comment(lib, "ws2_32.lib")

UINT64 statsThread(LPVOID parameters)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    Parameters* p = (Parameters*)parameters;
    SenderSocket* ss = (SenderSocket*)p->ss;

    clock_t start = clock();
    clock_t now;
    clock_t prev = start;

    int prevBase = ss->base;
    int nextBase;
    while (!p->stop)
    {
        Sleep(2000);
        now = clock();
        nextBase = ss->base;
        printf("[ %3d] B\t%8d ( %6.1f MB) N\t%8d T %4d F %4d W %6d S %7.3f Mbps RTT %.3f\n", (now - start) / CLOCKS_PER_SEC, ss->base, (max(ss->base - 1, 0) * (MAX_PKT_SIZE - sizeof(SenderDataHeader))) / (double)1e6, ss->nextSeq, ss->totalTimeouts, ss->totalFastRetransmit, ss->effectiveWindow, (double)(nextBase - prevBase) * 8 * (MAX_PKT_SIZE - sizeof(SenderDataHeader)) / (1e6 * ((double)(now - prev) / CLOCKS_PER_SEC)), ss->estRTT);
        prevBase = nextBase;
        prev = now;
    }
    return 0;
}

int main(int argc, char** argv)
{
    if (argc != 8)
    {
        printf("Invalid number of arguments.\nCorrect Usage: udpTL.exe <dest host> <power of 2 buffer size> <sender window in packets> <round-trip propagation delay in sec> <probability of loss forward dir> <probability of loss reverse dir> <speed of bottleneck in Mbps>\n");
        exit(1);
    }

    // 1. Parse command-line args:
    char* targetHost = argv[1];
    int power = atoi(argv[2]);
    int senderWindow = atoi(argv[3]);
    double speed = atof(argv[7]);

    if (speed <= 0.0 || (speed / 1000) > 10.0)
    {
        exit(1);
    }
    if (atof(argv[4]) >= 30.0 || atof(argv[4]) < 0.0)
    {
        exit(1);
    }
    if (atof(argv[5]) < 0.0 || atof(argv[5]) >= 1.0)
    {
        exit(1);
    }
    if (atof(argv[6]) < 0.0 || atof(argv[6]) >= 1.0)
    {
        exit(1);
    }
    if (senderWindow < 1 || senderWindow > 1e6)
    {
        exit(1);
    }

    LinkProperties lp;
    lp.RTT = (double)atof(argv[4]);
    lp.speed = (double)1e6 * atof(argv[7]);
    lp.pLoss[FORWARD_PATH] = (double)atof(argv[5]);
    lp.pLoss[RETURN_PATH] = (double)atof(argv[6]);

    // Print summary 
    printf("Main:\tsender W = %d, RTT = %.3f sec, loss %g / %g, link %d Mbps\n", senderWindow, lp.RTT, lp.pLoss[FORWARD_PATH], lp.pLoss[RETURN_PATH], (int)speed);

    // 2. Create DWORD buffer:
    printf("Main:\tinitializing DWORD array with 2^%d elements... ", power);
    clock_t start;
    start = clock(); // start clock

    UINT64 dwordBuffSize = (UINT64)1 << power;
    DWORD* dwordBuff = new DWORD[dwordBuffSize];
    memset(dwordBuff, 0, dwordBuffSize);
    // user-requested buffer
    for (int i = 0; i < dwordBuffSize; i++)      // required initialization of buffer
    {
        dwordBuff[i] = i;
    }
    printf("done in %d ms\n", 1000 * (clock() - start) / CLOCKS_PER_SEC);

    // 3. Set up UDP sender socket - SYN & SYN-ACK
    SenderSocket ss = SenderSocket(); // instace of SenderSocket class
    ss.senderWindow = senderWindow;

    int status;
    if ((status = ss.Open(targetHost, MAGIC_PORT, senderWindow, &lp)) != STATUS_OK)
    {
        printf("Main:\tconnect failed with status %d\n", status);
        exit(1);
    }
    printf("Main:\tconnected to %s in %.3f sec, packet size %d bytes\n", targetHost, lp.RTT, MAX_PKT_SIZE);

    /* Send function will go here */
    char* charBuf = (char*)dwordBuff;
    UINT64 byteBufferSize = dwordBuffSize << 2;

    struct Parameters statsPars;
    statsPars.ss = &ss;
    statsPars.stop = false;

    HANDLE statsHandle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)statsThread, &statsPars, 0, NULL);

    UINT64 off = 0;
    while (off < byteBufferSize)
    {
        UINT64 bytes = min(byteBufferSize - off, MAX_PKT_SIZE - sizeof(SenderDataHeader));
        if ((status = ss.Send(charBuf + off, bytes)) != STATUS_OK)
        {
            printf("send failed with status %d\n", status);
            exit(1);
        }
        off += bytes;
    }
    double time = (clock() - ss.timeCreated) / (double)CLOCKS_PER_SEC;
    statsPars.stop = true;
    if (statsHandle != NULL) {
        WaitForSingleObject(statsHandle, INFINITE);
        CloseHandle(statsHandle);
    }

    // 4. Close UDP sender socket - FIN & FIN-ACK

    if ((status = ss.Close(&lp, senderWindow, charBuf)) != STATUS_OK)
    {
        printf("Main:\tdisconnect failed with status %d\n", status);
        exit(1);
    }
    Checksum cs;
    DWORD check = cs.CRC32((unsigned char*)charBuf, byteBufferSize);
    printf("Main:\ttransfer finished in %.3f sec, %.2f Kbps, checksum %X\n", time, (double)dwordBuffSize * 32 / (double)(1000 * time), check);
    printf("Main:\testRTT %.3f, ideal rate %.2f Kbps\n", ss.estRTT, static_cast<unsigned long long>(8) * ss.senderWindow * (MAX_PKT_SIZE) / (ss.estRTT * 1000));

    // 5. Cleanup and finish
    closesocket(ss.sock);
    WSACleanup();
    return 0;
}