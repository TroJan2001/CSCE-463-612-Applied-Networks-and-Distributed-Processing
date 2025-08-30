//Semester: Spring/2025
//Course: CSCE 612
//Hw3P3

#include "pch.h"

using namespace std;

// Create an ICMP Echo Request packet
void prepare_icmp_request(ICMPHeader* icmp, u_short pid, u_short seq) {
    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0;
    icmp->id = pid;
    icmp->seq = seq;
    icmp->checksum = 0;
    icmp->checksum = ip_checksum((u_short*)icmp, sizeof(ICMPHeader));
}

// Send an ICMP packet with a given TTL
bool send_icmp_packet(SOCKET sock, struct sockaddr_in* dest, char* send_buf, u_short pid, int ttl, HopResult* hops) {
    if (setsockopt(sock, IPPROTO_IP, IP_TTL, (const char*)&ttl, sizeof(ttl)) == SOCKET_ERROR) {
        printf("setsockopt failed at TTL %d\n", ttl);
        return false;
    }

    ICMPHeader* icmp = (ICMPHeader*)send_buf;
    prepare_icmp_request(icmp, pid, (u_short)ttl);

    int ret = sendto(sock, send_buf, sizeof(ICMPHeader), 0, (SOCKADDR*)dest, sizeof(*dest));
    if (ret == SOCKET_ERROR) {
        printf("sendto failed at TTL %d\n", ttl);
        return false;
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    hops[ttl].send_time = now; // Record send time in HopResult
    return true;
}

// Cleanup memory and socket
void cleanup(SOCKET sock, char* send_buf, char* recv_buf) {
    closesocket(sock);
    WSACleanup();
    free(send_buf);
    free(recv_buf);
}

// Compute adaptive timeout for retransmission
double compute_timeout_for_hop(int ttl, HopResult* hops) {
    double timeout_ms = 1000.0; // default: 1000 ms

    if (ttl > 1 && ttl < 30) {
        double rtt_before = hops[ttl - 1].replied ? hops[ttl - 1].rtt_ms : -1;
        double rtt_after = hops[ttl + 1].replied ? hops[ttl + 1].rtt_ms : -1;

        if (rtt_before > 0 && rtt_after > 0) {
            timeout_ms = 2.0 * (rtt_before + rtt_after) / 2.0;
        }
        else if (rtt_before > 0) {
            timeout_ms = 2.0 * rtt_before;
        }
        else if (rtt_after > 0) {
            timeout_ms = 2.0 * rtt_after;
        }
    }

    if (timeout_ms < 100.0) timeout_ms = 100;
    if (timeout_ms > 500) timeout_ms = 500;

    return timeout_ms;
}

void send_dns_query(SOCKET dns_sock, const char* ip, unsigned short* out_txid) {
    struct sockaddr_in dns_server;
    dns_server.sin_family = AF_INET;
    dns_server.sin_port = htons(53);
    inet_pton(AF_INET, "192.168.1.1", &(dns_server.sin_addr)); // local DNS

    char query[MAX_DNS_SIZE];
    unsigned short txid = rand() % 0xFFFF; // generate random TXID
    createDNSQuery(query, txid, ip);

    int query_size = sizeof(DNSHeader) + strlen(query + sizeof(DNSHeader)) + 1 + sizeof(DNSQuestion);

    sendto(dns_sock, query, query_size, 0, (SOCKADDR*)&dns_server, sizeof(dns_server));

    if (out_txid)
        *out_txid = txid;  // return the TXID to caller for tracking
}

bool receive_dns_reply(SOCKET dns_sock, unsigned short expected_txid, char* resolved_name) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(dns_sock, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 2; // 2 seconds
    timeout.tv_usec = 0;

    int ready = select(0, &readfds, NULL, NULL, &timeout);
    if (ready > 0) {
        struct sockaddr_in from;
        int fromlen = sizeof(from);
        char response[MAX_DNS_SIZE];

        int bytes = recvfrom(dns_sock, response, sizeof(response), 0, (SOCKADDR*)&from, &fromlen);
        if (bytes > 0) {
            DNSHeader* hdr = (DNSHeader*)response;
            unsigned short txid = ntohs(hdr->TXID);

            if (txid == expected_txid) {
                return parseDNSReply(response, bytes, expected_txid, resolved_name);
            }
        }
    }

    return false;
}

// Main program
int main(int argc, char** argv)
{
    srand(time(0));
    WSADATA wsaData;
    SOCKET sock;
    SOCKET dns_sock;

    struct sockaddr_in dest;
    char* send_buf = (char*)malloc(MAX_ICMP_SIZE);
    char* recv_buf = (char*)malloc(MAX_REPLY_SIZE);
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    if (!send_buf || !recv_buf) {
        printf("Memory allocation failed!\n");
        return -1;
    }

    HopResult hops[31];
    int attempts[31] = { 0 };
    int max_ttl = 30;
    bool finished = false;

    for (int i = 1; i <= 30; i++) {
        hops[i].replied = false;
    }

    u_short pid = (u_short)GetCurrentProcessId();

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed\n");
        return -1;
    }

    sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock == INVALID_SOCKET) {
        printf("Unable to create socket\n");
        cleanup(sock, send_buf, recv_buf);
        return -1;
    }

    dns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (dns_sock == INVALID_SOCKET) {
        printf("Unable to create socket\n");
        cleanup(dns_sock, send_buf, recv_buf);
        return -1;
    }

    LARGE_INTEGER start_clock;
    QueryPerformanceCounter(&start_clock);

    // Resolve destination
    struct addrinfo hints = { 0 };
    struct addrinfo* result = NULL;
    hints.ai_family = AF_INET;

    if (argc < 2) {
        printf("Usage: %s <destination IP or hostname>\n", argv[0]);
        return -1;
    }

    int ret = getaddrinfo(argv[1], NULL, &hints, &result);
    if (ret != 0 || result == NULL) {
        printf("DNS lookup failed!\n");
        cleanup(sock, send_buf, recv_buf);
        return -1;
    }

    struct sockaddr_in* addr = (struct sockaddr_in*)result->ai_addr;
    dest.sin_family = AF_INET;
    dest.sin_addr = addr->sin_addr;
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr->sin_addr), ip_str, sizeof(ip_str));

    printf("Tracerouting to %s (%s)...\n", argv[1], ip_str);
    freeaddrinfo(result);

    // Send 30 packets (in parallel)
    for (int ttl = 1; ttl <= max_ttl; ttl++) {
        if (send_icmp_packet(sock, &dest, send_buf, pid, ttl, hops)) {
            attempts[ttl] = 1;
        }
    }

    while (true) {
        fd_set readfds;
        struct timeval timeout;
        double est_timeout = 1000.0;

        for (int i = 1; i <= max_ttl; i++) {
            if (!hops[i].replied && attempts[i] < 3) {
                est_timeout = compute_timeout_for_hop(i, hops);
                break;
            }
        }

        timeout.tv_sec = (int)(est_timeout/1000);
        timeout.tv_usec = (int)((est_timeout - timeout.tv_sec * 1000) * 1000);


        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        FD_SET(dns_sock, &readfds);

        int ready = select(0, &readfds, NULL, NULL, &timeout);

        if (ready > 0) {
            if (FD_ISSET(sock, &readfds)) {
                struct sockaddr_in from;
                int fromlen = sizeof(from);
                int bytes = recvfrom(sock, recv_buf, MAX_REPLY_SIZE, 0, (SOCKADDR*)&from, &fromlen);

                if (bytes != SOCKET_ERROR && bytes >= 56) {
                    IPHeader* outer_ip_hdr = (IPHeader*)recv_buf;
                    ICMPHeader* outer_icmp_hdr = (ICMPHeader*)(recv_buf + sizeof(IPHeader));
                    int seq = 0;

                    if (outer_icmp_hdr->type == ICMP_TTL_EXPIRED) {
                        IPHeader* orig_ip_hdr = (IPHeader*)(outer_icmp_hdr + 1);
                        ICMPHeader* orig_icmp_hdr = (ICMPHeader*)(orig_ip_hdr + 1);
                        if (orig_icmp_hdr->id == pid) {
                            seq = orig_icmp_hdr->seq;
                        }
                    }
                    else if (outer_icmp_hdr->type == ICMP_ECHO_REPLY) {
                        if (outer_icmp_hdr->id == pid) {
                            seq = outer_icmp_hdr->seq;
                            max_ttl = min(max_ttl, seq);
                        }
                    }
                    else if (outer_icmp_hdr->type == ICMP_DEST_UNREACH) {
                        IPHeader* orig_ip_hdr = (IPHeader*)(outer_icmp_hdr + 1);
                        ICMPHeader* orig_icmp_hdr = (ICMPHeader*)(orig_ip_hdr + 1);

                        if (orig_icmp_hdr->id == pid) {
                            seq = orig_icmp_hdr->seq;
                            if (seq >= 1 && seq <= 30 && !hops[seq].replied) {
                                hops[seq].ttl = seq;
                                inet_ntop(AF_INET, &(from.sin_addr), hops[seq].ipstr, sizeof(hops[seq].ipstr));
                                LARGE_INTEGER recv_now;
                                QueryPerformanceCounter(&recv_now);
                                hops[seq].rtt_ms = (recv_now.QuadPart - hops[seq].send_time.QuadPart) * 1000.0 / freq.QuadPart;
                                hops[seq].replied = true;

                                sprintf_s(hops[seq].error_string, sizeof(hops[seq].error_string), "Destination Unreachable (code %d)", outer_icmp_hdr->code);
                            }
                        }
                    }
                    else {
                        printf("Got other ICMP type = %d, code = %d\n", outer_icmp_hdr->type, outer_icmp_hdr->code);
                    }

                    if (seq >= 1 && seq <= 30 && !hops[seq].replied) {
                        hops[seq].ttl = seq;
                        inet_ntop(AF_INET, &(from.sin_addr), hops[seq].ipstr, sizeof(hops[seq].ipstr));
                        LARGE_INTEGER recv_now;
                        QueryPerformanceCounter(&recv_now);
                        hops[seq].rtt_ms = (recv_now.QuadPart - hops[seq].send_time.QuadPart) * 1000.0 / freq.QuadPart;
                        hops[seq].replied = true;
                        unsigned short txid;
                        send_dns_query(dns_sock, hops[seq].ipstr, &txid);
                        hops[seq].dns_txid = txid;
                        hops[seq].dns_sent = true;
                        hops[seq].dns_replied = false;
                    }

                }
            }
            if (FD_ISSET(dns_sock, &readfds)) {
                struct sockaddr_in dns_from;
                int dns_fromlen = sizeof(dns_from);
                char dns_response[MAX_DNS_SIZE];
                int dns_bytes = recvfrom(dns_sock, dns_response, sizeof(dns_response), 0, (SOCKADDR*)&dns_from, &dns_fromlen);

                if (dns_bytes > 0) {
                    DNSHeader* hdr = (DNSHeader*)dns_response;
                    unsigned short txid = ntohs(hdr->TXID);

                    for (int i = 1; i <= max_ttl; i++) {
                        if (hops[i].dns_sent && !hops[i].dns_replied && hops[i].dns_txid == txid) {
                            if (parseDNSReply(dns_response, dns_bytes, txid, hops[i].resolved_name)) {
                                hops[i].dns_replied = true;
                            }
                            else {
                                strcpy_s(hops[i].resolved_name, sizeof(hops[i].resolved_name), "<no DNS entry>");
                                hops[i].dns_replied = true;
                            }
                            break;
                        }
                    }
                }
            }

        }

        finished = true;
        for (int i = 1; i <= max_ttl; i++) {
            if (!hops[i].replied && attempts[i] < 3) {
                LARGE_INTEGER now;
                QueryPerformanceCounter(&now);

                double elapsed = (now.QuadPart - hops[i].send_time.QuadPart) * 1000.0 / freq.QuadPart;
                double timeout_ms = compute_timeout_for_hop(i, hops);

                if (elapsed >= timeout_ms) {
                    send_icmp_packet(sock, &dest, send_buf, pid, i, hops);
                    hops[i].send_time = now;
                    attempts[i]++;
                    finished = false;
                }
                else {
                    finished = false;
                }
            }
        }


        if (finished) {
            break;
        }
    }


    for (int i = 1; i <= max_ttl; i++) {
        if (hops[i].replied) {
            if (!hops[i].dns_replied || strlen(hops[i].resolved_name) == 0)
                strcpy_s(hops[i].resolved_name, sizeof(hops[i].resolved_name), "<no DNS entry>");

            printf("%2d  %s (%s)  %.3f ms (%d)\n", i, hops[i].resolved_name, hops[i].ipstr, hops[i].rtt_ms, attempts[i]);
        }
        else {
            printf("%2d  * (%d)\n", i, attempts[i]);
        }
    }



    LARGE_INTEGER end_clock;
    QueryPerformanceCounter(&end_clock);
    printf("Total execution time: %.0f ms\n", (end_clock.QuadPart - start_clock.QuadPart) * 1000.0 / freq.QuadPart);

    cleanup(sock, send_buf, recv_buf);
    return 0;
}
