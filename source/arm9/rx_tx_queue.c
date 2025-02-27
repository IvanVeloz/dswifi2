// SPDX-License-Identifier: MIT
//
// Copyright (C) 2005-2006 Stephen Stair - sgstair@akkit.org - http://www.akkit.org
// Copyright (C) 2025 Antonio Niño Díaz

#include <nds.h>

#include "arm9/ipc.h"
#include "arm9/wifi_arm9.h"
#include "common/common_defs.h"

// TX functions
// ============

u32 Wifi_TxBufferBytesAvailable(void)
{
    s32 size = WifiData->txbufIn - WifiData->txbufOut - 1;
    if (size < 0)
        size += WIFI_TXBUFFER_SIZE / 2;

    return size * 2;
}

void Wifi_TxBufferWrite(u32 base, u32 size_bytes, const void *src)
{
    sassert((base & 1) == 0, "Unaligned base address");

    const u16 *in = src;

    // Convert to halfwords
    base = base / 2;
    int write_halfwords = (size_bytes + 1) / 2; // Round up

    while (write_halfwords > 0)
    {
        int writelen = write_halfwords;
        if (writelen > (WIFI_TXBUFFER_SIZE / 2) - base)
            writelen = (WIFI_TXBUFFER_SIZE / 2) - base;

        write_halfwords -= writelen;

        while (writelen)
        {
            WifiData->txbufData[base] = *in;
            in++;
            base++;
            writelen--;
        }

        base = 0;
    }
}

// Length specified in bytes.
int Wifi_RawTxFrame(u16 datalen, u16 rate, const void *src)
{
    int sizeneeded = datalen + HDR_TX_SIZE + 1;
    if (sizeneeded > Wifi_TxBufferBytesAvailable())
    {
        WifiData->stats[WSTAT_TXQUEUEDREJECTED]++;
        return -1;
    }

    Wifi_TxHeader txh = { 0 };

    txh.tx_rate   = rate;
    txh.tx_length = datalen + 4; // FCS

    // TODO: Replace this by a mutex?
    int oldIME = enterCriticalSection();

    int base = WifiData->txbufOut;
    {
        Wifi_TxBufferWrite(base * 2, sizeof(txh), &txh);

        base += sizeof(txh) / 2;
        if (base >= (WIFI_TXBUFFER_SIZE / 2))
            base -= WIFI_TXBUFFER_SIZE / 2;

        Wifi_TxBufferWrite(base * 2, datalen, src);

        base += (datalen + 1) / 2;
        if (base >= (WIFI_TXBUFFER_SIZE / 2))
            base -= WIFI_TXBUFFER_SIZE / 2;
    }
    WifiData->txbufOut = base;

    leaveCriticalSection(oldIME);

    WifiData->stats[WSTAT_TXQUEUEDPACKETS]++;
    WifiData->stats[WSTAT_TXQUEUEDBYTES] += sizeneeded;

    Wifi_CallSyncHandler();

    return 0;
}

// RX functions
// ============

void Wifi_RxRawReadPacket(u32 base, u32 size_bytes, void *dst)
{
    if (base & 1)
    {
        sassert(0, "Unaligned base address");
        return;
    }

    u16 *out = dst;

    // Convert to halfwords
    base = base / 2;
    int read_hwords = (size_bytes + 1) / 2; // Round up

    while (read_hwords > 0)
    {
        int len = read_hwords;
        if (len > (WIFI_RXBUFFER_SIZE / 2) - base)
            len = (WIFI_RXBUFFER_SIZE / 2) - base;

        read_hwords -= len;

        while (len > 0)
        {
            *out = WifiData->rxbufData[base++];
            out++;
            len--;
        }

        base = 0;
    }
}

u16 Wifi_RxReadHWordOffset(u32 base, u32 offset)
{
    sassert(((base | offset) & 1) == 0, "Unaligned arguments");

    base = (base + offset) / 2;
    if (base >= (WIFI_RXBUFFER_SIZE / 2))
        base -= (WIFI_RXBUFFER_SIZE / 2);

    return WifiData->rxbufData[base];
}
