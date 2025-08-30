#include "pch.h"


u_short ip_checksum(u_short* buffer, int size)
{
    u_long cksum = 0;
    while (size > 1)
    {
        cksum += *buffer++;
        size -= sizeof(u_short);
    }
    if (size)
        cksum += *(u_char*)buffer;
    cksum = (cksum >> 16) + (cksum & 0xffff);
    return (u_short)(~cksum);
}
