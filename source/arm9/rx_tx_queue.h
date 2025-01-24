// SPDX-License-Identifier: MIT
//
// Copyright (C) 2005-2006 Stephen Stair - sgstair@akkit.org - http://www.akkit.org
// Copyright (C) 2025 Antonio Niño Díaz

#ifndef DSWIFI_ARM9_RX_TX_QUEUE_H__
#define DSWIFI_ARM9_RX_TX_QUEUE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <nds/ndstypes.h>

// Returns the number of bytes available in the TX buffer.
u32 Wifi_TxBufferBytesAvailable(void);

// Start and length are specified in bytes.
//
// The base address must be aligned to 16 bits.
//
// The length doesn't need to be a multiple of two, but it will be read with 16
// bit reads. If the value isn't a multiple of two, one more byte will be read
// to do the last read.
//
// TODO: Handle this special case in the function to only read one byte and fill
// the rest with 0?
void Wifi_TxBufferWrite(u32 base, u32 size_bytes, const u16 *dst);

// Length specified in bytes.
int Wifi_RawTxFrame(u16 datalen, u16 rate, u16 *data);

// The base address is specified in halfwords. The size is specified in bytes.
void Wifi_RxRawReadPacket(u32 base, u32 size_bytes, u16 *dst);

// The base address and offset are specified in halfwords.
u16 Wifi_RxReadHWordOffset(u32 base, u32 offset);

#ifdef __cplusplus
};
#endif

#endif // DSWIFI_ARM9_RX_TX_QUEUE_H__
