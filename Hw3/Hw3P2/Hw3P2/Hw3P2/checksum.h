#pragma once
#ifndef CHECKSUM_H
#define CHECKSUM_H

#include <windows.h>
#include <cstddef>

class Checksum {
private:
    DWORD crc_table[256];

public:
    Checksum(); // constructor builds the table
    DWORD CRC32(unsigned char* buf, size_t len);
};

#endif // CHECKSUM_H
