//Semester: Spring/2025
//Course: CSCE 612
//Hw3P1

#include "pch.h"

using namespace std;

int main(int argc, char** argv) {
    if (argc != 8) {
        std::cerr << "Usage: " << argv[0] << " <server> <power> <window> <RTT> <loss_fwd> <loss_ret> <speed>\n";
        return 1;
    }

    char* targetHost = argv[1];
    int power = atoi(argv[2]);
    int senderWindow = atoi(argv[3]);
    float RTT = (float)atof(argv[4]);
    float lossFwd = (float)atof(argv[5]);
    float lossRet = (float)atof(argv[6]);
    float speed = (float)(atof(argv[7]) * 1e6); // Mbps to bits/sec

    // Summary printout
    cout << "Main:   sender W = " << senderWindow
        << ", RTT " << fixed << setprecision(3) << RTT
        << " sec, loss " << setprecision(6) << lossFwd
        << " / " << setprecision(6) << lossRet
        << ", link " << (int)(speed / 1e6) << " Mbps\n";

    // Buffer setup
    cout << "Main:   initializing DWORD array with 2^" << power << " elements... ";
    auto initStart = chrono::high_resolution_clock::now();
    UINT64 dwordBufSize = (UINT64)1 << power;
    DWORD* dwordBuf = new DWORD[dwordBufSize];
    for (UINT64 i = 0; i < dwordBufSize; ++i)
        dwordBuf[i] = i;
    auto initEnd = chrono::high_resolution_clock::now();

    auto duration = chrono::duration_cast<chrono::milliseconds>(initEnd - initStart).count();
    cout << "done in " << duration << " ms\n";


    // Link properties setup
    LinkProperties lp;
    lp.RTT = RTT;
    lp.speed = speed;
    lp.pLoss[FORWARD_PATH] = lossFwd;
    lp.pLoss[RETURN_PATH] = lossRet;

    SenderSocket ss;
    double start = GetTime();
    int status = ss.Open(targetHost, senderWindow, &lp);
    double openTime = GetTime();

    if (status != STATUS_OK) {
        cerr << "Main:   connect failed with status " << status << "\n";
        delete[] dwordBuf;
        return status;
    }

    // Compute packet size
    UINT64 byteBufferSize = dwordBufSize << 2; // DWORD = 4 bytes
    char* charBuf = (char*)dwordBuf;
    cout << "Main:   connected to " << targetHost
        << " in " << fixed << setprecision(3)
        << (openTime - start) << " sec, pkt size "
        << MAX_PKT_SIZE << " bytes\n";

    // (No ss.Send yet — not required in Part 1)

    double beforeClose = GetTime();
    status = ss.Close();
    double afterClose = GetTime();

    if (status != STATUS_OK) {
        cerr << "Main:   close failed with status " << status << "\n";
        delete[] dwordBuf;
        return status;
    }

    cout << "Main:   transfer finished in " << fixed
        << setprecision(3) << (afterClose - beforeClose)
        << " sec\n";

    delete[] dwordBuf;
    return STATUS_OK;
}
