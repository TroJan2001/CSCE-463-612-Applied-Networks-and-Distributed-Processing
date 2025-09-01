# ✅ CSCE 463/612 – Applied Networks and Distributed Processing  
## 📚 Project Summary: Homework 1–4 (Spring 2025)

This repository contains implementations and documentation for Homework 1 through Homework 4, covering core networking concepts and systems programming techniques using **C++** and **Windows Sockets (Winsock)**. Assignments build from a basic socket-based HTTP client to a multi-threaded web crawler, a reliable transport-layer protocol over UDP, a custom DNS resolver, and a parallel traceroute tool.

---

## 🧩 Homework 1 – Web Crawler in C++

### ✅ Part 1 – Basic Single URL Web Client  
📄 **File:** `hw1p1.pdf`

🔹 **Purpose:**  
Build a single-threaded HTTP/1.0 client that:
- Accepts a URL from the command line  
- Connects over TCP and sends a GET request  
- Measures timing (DNS, connect, download)  
- Parses headers, extracts page size, and counts links

🧠 **Key Concepts:**
- Winsock API (`connect`, `send`, `recv`)
- Select-based timeout
- Dynamic buffer resizing
- HTML parsing with a custom library

---

### ✅ Part 2 – File-Based Crawling (Single Thread)  
📄 **File:** `hw1p2.pdf`

🔹 **Purpose:**  
Extend Part 1 to process a file of URLs, still single-threaded.

🧩 **Features:**
- Ensures unique host/IP crawling  
- Fetches and parses `robots.txt`  
- Applies download limits (2MB, 10s, 16KB for robots)  
- Logs detailed trace per site

🧠 **Key Concepts:**
- HEAD requests and polite crawling
- Error handling, memory efficiency

---

### ✅ Part 3 – Multi-threaded Web Crawler  
📄 **File:** `hw1p3.pdf`

🔹 **Purpose:**  
Parallelize the crawler using multiple threads and shared queues.

🧩 **Features:**
- CLI: `hw1.exe <N_threads> <input_file>`  
- N crawler threads + 1 stats thread  
- Real-time throughput reporting  
- Detailed summary and link analysis  

📊 **Example Output:**
[10] 3500 Q 123456 E 54321 H 4321 D 3210 I 2100 R 1900 C 1800 L 12K
*** crawling 456.3 pps @ 78.3 Mbps

markdown
Copy code

🧠 **Key Concepts:**
- Thread synchronization (`std::mutex`)
- Producer-consumer model
- Non-thread-safe parser isolation
- Optional: chunked transfer decoding

---

## 📡 Homework 2 – DNS Resolver & Server  
📄 **File:** `hw2.pdf`

🔹 **Purpose:**  
Implement a full custom DNS client and server from scratch.

## 🧩 Features
- Raw UDP socket communication
- Manual DNS packet encoding and decoding
- Recursive resolution via RD flag
- A, NS, CNAME, and PTR record support
- Reverse DNS lookup (in-addr.arpa format)
- Timeout (10s) and up to 3 retry attempts
- Comprehensive DNS response parser:
  - Header, Question, Answer, Authority, Additional sections
- Fully handles:
  - Name compression (jump pointers)
  - Multi-section parsing with bounds checking
  - TTL, class, and record length validation

---

## 🌟 Extra Credit (Implemented ✅)
- Detects and handles all **9 required** malformed DNS error cases:
  - Jump loop
  - Truncated header
  - Jump into header
  - Oversized label
  - Label outside bounds
  - Name exceeds 255 bytes
  - Truncated RR
  - Excessive TTL
  - Invalid class

Each error is printed with a `++ invalid record:` message.

---

## 🧠 Key Concepts
- DNS protocol (RFC 1034 / RFC 1035)
- DNS name compression and pointer dereferencing
- Socket-level programming with UDP
- Timeout and retry logic via `select()`
- Bitwise flag construction and header packing
- Defensive programming & packet boundary checking
---

## 📦 Homework 3 – Reliable Data Transfer Protocol

### ✅ Part 1 – rdt 2.1 with Stop-and-Wait  
📄 **File:** `hw3p1.pdf`

🔹 **Purpose:**  
Implement reliable UDP transmission with acknowledgments, sequence numbers, and retransmissions.

🧠 **Key Concepts:**
- Packet-based framing  
- Timeout retransmission  
- Simulated packet loss/corruption  
- Basic stats reporting

---

### ✅ Part 2 – rdt 3.0 with CRC + Adaptive Timeout  
📄 **File:** `hw3p2.pdf`

🔹 **Purpose:**  
Add dynamic RTO estimation and CRC-32 checksumming.

🧩 **Features:**
- TCP-style RTT + RTO estimation  
- CRC validation for data integrity  
- Stats thread with live metrics (throughput, RTT, loss)  
- Graceful `Close()` with complete ACK reception

🧠 **Key Concepts:**
- `RTO = estRTT + 4 * max(devRTT, 10ms)`  
- CRC-32 calculation  
- Select-based timing and cleanup

---

### ✅ Part 3 – Sliding Window Protocol (rdt Full TCP-style)  
📄 **File:** `hw3p3.pdf`

🔹 **Purpose:**  
Implement a pipelined, window-based transport layer protocol similar to TCP.

🧩 **Features:**
- Sliding window with cumulative ACKs  
- Fast retransmit, selective repeat  
- Flow control via receiver window  
- Max 50 retransmissions per packet  
- Stats print every 2 seconds

📊 **Analysis Report:**
- Plot throughput vs. window size  
- Evaluate RTT, packet loss effects

🧠 **Key Concepts:**
- Threading + synchronization  
- Efficient buffering and cleanup  
- Socket buffer tuning, event management

---

## 🌍 Homework 4 – Parallel Traceroute Tool  
📄 **File:** `hw4.pdf`

🔹 **Purpose:**  
Build a parallel version of traceroute using raw ICMP packets to efficiently measure routing paths to remote hosts.

🧩 **Features:**
- ICMP Echo Requests with increasing TTLs  
- Sends all TTL probes in parallel (not sequential)  
- Reverse DNS lookups and RTT tracking per hop  
- Per-hop retransmissions with adaptive timeout (RTO)  
- Min-heap scheduling for efficient retx management  
- ICMP error detection and reporting

📊 **Example Output:**
5 <no DNS entry> (10.3.3.57) 0.734 ms (2)
...
25 p21.www.scd.yahoo.com (66.94.230.52) 84.435 ms (1)
Total execution time: 650 ms

pgsql
Copy code

🧠 **Key Concepts:**
- Raw ICMP sockets and TTL-limited probing  
- Asynchronous or multi-threaded DNS resolution  
- RTT-based timeout adaptation  
- Parsing TTL expired, echo reply, and ICMP error messages

✏️ **Report Includes:**
- Histogram of hop counts to 10K responsive IPs  
- Delay distribution with 50ms bins  
- Longest path trace analysis  
- Design for minimizing redundant probes in batch-mode (extra credit)

---

## 🗂 Summary Table

| Homework   | Focus                         | Threads / Concurrency         | Key Features                                                                 |
| ---------- | ----------------------------- | ----------------------------- | ---------------------------------------------------------------------------- |
| **HW1 P1** | HTTP GET client               | 1 (single-threaded)           | URL parsing, TCP GET request, DNS/Connect/Download timing, HTML link parsing |
| **HW1 P2** | File-based web crawler        | 1 (single-threaded)           | URL deduplication, robots.txt, download limits, trace logging                |
| **HW1 P3** | Multi-threaded web crawler    | N crawler threads + 1 stats   | Shared queues, real-time stats, polite crawling, link classification         |
| **HW2**    | DNS client and server         | Single-threaded or async      | A, NS, CNAME, PTR support, UDP sockets, retries, iterative/recursive modes   |
| **HW3 P1** | rdt 2.1 (stop-and-wait)       | 1 (single-threaded)           | Reliable UDP connect/close only, sequence numbers, simulated loss            |
| **HW3 P2** | rdt 3.0 with CRC + RTO        | 1 + stats thread              | CRC-32 checks, RTT + RTO estimation, sender with stop-and-wait               |
| **HW3 P3** | Sliding window protocol (rdt) | 1 main + 1 stats + 1 worker   | Pipelined send, cumulative ACKs, fast retransmit, flow control, queue model  |
| **HW4**    | Parallel traceroute           | 1 main + async DNS via select | Raw ICMP with TTL, per-hop RTO, DNS lookups, ICMP error handling             |
