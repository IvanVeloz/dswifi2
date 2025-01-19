// SPDX-License-Identifier: MIT
//
// Copyright (C) 2005-2006 Stephen Stair - sgstair@akkit.org - http://www.akkit.org
// Copyright (C) 2025 Antonio Niño Díaz

#include <nds.h>

#include "arm7/wifi_arm7.h"
#include "arm7/wifi_baseband.h"
#include "arm7/wifi_flash.h"
#include "arm7/wifi_ipc.h"
#include "arm7/wifi_mac.h"
#include "arm7/wifi_rf.h"
#include "common/spinlock.h"

volatile Wifi_MainStruct *WifiData = 0;

int keepalive_time = 0;

//////////////////////////////////////////////////////////////////////////
//
//  Other support
//

static int wifi_led_state = 0;

static void Wifi_SetLedState(int state)
{
    if (WifiData->flags9 & WFLAG_ARM9_USELED)
    {
        if (wifi_led_state != state)
        {
            wifi_led_state = state;
            ledBlink(state);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
//
//  Main functionality
//

void Wifi_TxSetup(void)
{
#if 0
    switch(W_MODE_WEP & 7)
    {
        case 0: //
            // 4170,  4028, 4000
            // TxqEndData, TxqEndManCtrl, TxqEndPsPoll
            W_MACMEM(0x24) = 0xB6B8;
            W_MACMEM(0x26) = 0x1D46;
            W_MACMEM(0x16C) = 0xB6B8;
            W_MACMEM(0x16E) = 0x1D46;
            W_MACMEM(0x790) = 0xB6B8;
            W_MACMEM(0x792) = 0x1D46;
            W_TXREQ_SET = 1;
            break;

        case 1: //
            // 4AA0, 4958, 4334
            // TxqEndData, TxqEndManCtrl, TxqEndBroadCast
            // 4238, 4000
            W_MACMEM(0x234) = 0xB6B8;
            W_MACMEM(0x236) = 0x1D46;
            W_MACMEM(0x330) = 0xB6B8;
            W_MACMEM(0x332) = 0x1D46;
            W_MACMEM(0x954) = 0xB6B8;
            W_MACMEM(0x956) = 0x1D46;
            W_MACMEM(0xA9C) = 0xB6B8;
            W_MACMEM(0xA9E) = 0x1D46;
            W_MACMEM(0x10C0) = 0xB6B8;
            W_MACMEM(0x10C2) = 0x1D46;
            //...
            break;

        case 2:
            // 45D8, 4490, 4468
            // TxqEndData, TxqEndManCtrl, TxqEndPsPoll

            W_MACMEM(0x230) = 0xB6B8;
            W_MACMEM(0x232) = 0x1D46;
            W_MACMEM(0x464) = 0xB6B8;
            W_MACMEM(0x466) = 0x1D46;
            W_MACMEM(0x48C) = 0xB6B8;
            W_MACMEM(0x48E) = 0x1D46;
            W_MACMEM(0x5D4) = 0xB6B8;
            W_MACMEM(0x5D6) = 0x1D46;
            W_MACMEM(0xBF8) = 0xB6B8;
            W_MACMEM(0xBFA) = 0x1D46;
#endif
    W_TXREQ_SET = 0x000D;
#if 0
    }
#endif
}

void Wifi_RxSetup(void)
{
    W_RXCNT = 0x8000;
#if 0
    switch(W_MODE_WEP & 7)
    {
        case 0:
            W_RXBUF_BEGIN = 0x4794;
            W_RXBUF_WR_ADDR = 0x03CA;
            // 17CC ?
            break;
        case 1:
            W_RXBUF_BEGIN = 0x50C4;
            W_RXBUF_WR_ADDR = 0x0862;
            // 0E9C ?
            break;
        case 2:
            W_RXBUF_BEGIN = 0x4BFC;
            W_RXBUF_WR_ADDR = 0x05FE;
            // 1364 ?
            break;
        case 3:
            W_RXBUF_BEGIN = 0x4794;
            W_RXBUF_WR_ADDR = 0x03CA;
            // 17CC ?
            break;
    }
#endif
    W_RXBUF_BEGIN    = 0x4C00;
    W_RXBUF_WR_ADDR  = 0x0600;

    W_RXBUF_END     = 0x5F60;
    W_RXBUF_READCSR = (W_RXBUF_BEGIN & 0x3FFF) >> 1;
    W_RXBUF_GAP     = 0x5F5E;
    W_RXCNT         = 0x8001;
}

void Wifi_WakeUp(void)
{
    W_POWER_US = 0;

    swiDelay(67109); // 8ms delay

    Wifi_BBPowerOn();

    // Unset and set bit 7 of register 1 to reset the baseband
    u32 i = Wifi_BBRead(1);
    Wifi_BBWrite(1, i & 0x7f);
    Wifi_BBWrite(1, i);

    swiDelay(335544); // 40ms delay

    Wifi_RFInit();
}

void Wifi_Shutdown(void)
{
    if (Wifi_FlashReadByte(0x40) == 2)
        Wifi_RFWrite(0xC008);

    int a = Wifi_BBRead(0x1E);
    Wifi_BBWrite(REG_MM3218_EXT_GAIN, a | 0x3F);

    Wifi_BBPowerOff();

    W_POWER_US = 1;
}

void Wifi_CopyMacAddr(volatile void *dest, volatile void *src)
{
    ((u16 *)dest)[0] = ((u16 *)src)[0];
    ((u16 *)dest)[1] = ((u16 *)src)[1];
    ((u16 *)dest)[2] = ((u16 *)src)[2];
}

int Wifi_CmpMacAddr(volatile void *mac1, volatile void *mac2)
{
    return (((u16 *)mac1)[0] == ((u16 *)mac2)[0]) && (((u16 *)mac1)[1] == ((u16 *)mac2)[1])
           && (((u16 *)mac1)[2] == ((u16 *)mac2)[2]);
}

//////////////////////////////////////////////////////////////////////////
//
//  MAC Copy functions
//

int Wifi_QueueRxMacData(u32 base, u32 len)
{
    int buflen, temp, macofs, tempout;
    macofs = 0;
    buflen = (WifiData->rxbufIn - WifiData->rxbufOut - 1) * 2;
    if (buflen < 0)
        buflen += WIFI_RXBUFFER_SIZE;
    if (buflen < len)
    {
        WifiData->stats[WSTAT_RXQUEUEDLOST]++;
        return 0;
    }
    WifiData->stats[WSTAT_RXQUEUEDPACKETS]++;
    WifiData->stats[WSTAT_RXQUEUEDBYTES] += len;
    temp    = WIFI_RXBUFFER_SIZE - (WifiData->rxbufOut * 2);
    tempout = WifiData->rxbufOut;
    if (len > temp)
    {
        Wifi_MACCopy((u16 *)WifiData->rxbufData + tempout, base, macofs, temp);
        macofs += temp;
        len -= temp;
        tempout = 0;
    }
    Wifi_MACCopy((u16 *)WifiData->rxbufData + tempout, base, macofs, len);
    tempout += len / 2;
    if (tempout >= (WIFI_RXBUFFER_SIZE / 2))
        tempout -= (WIFI_RXBUFFER_SIZE / 2);
    WifiData->rxbufOut = tempout;

    Wifi_CallSyncHandler();

    return 1;
}

int Wifi_CheckTxBuf(s32 offset)
{
    offset += WifiData->txbufIn;
    if (offset >= WIFI_TXBUFFER_SIZE / 2)
        offset -= WIFI_TXBUFFER_SIZE / 2;
    return WifiData->txbufData[offset];
}

// non-wrapping function.
int Wifi_CopyFirstTxData(s32 macbase)
{
    int seglen, readbase, max, packetlen, length;
    packetlen = Wifi_CheckTxBuf(5);
    readbase  = WifiData->txbufIn;
    length    = (packetlen + 12 - 4 + 1) / 2;
    max       = WifiData->txbufOut - WifiData->txbufIn;
    if (max < 0)
        max += WIFI_TXBUFFER_SIZE / 2;
    if (max < length)
        return 0;

    while (length > 0)
    {
        seglen = length;
        if (readbase + seglen > WIFI_TXBUFFER_SIZE / 2)
            seglen = WIFI_TXBUFFER_SIZE / 2 - readbase;
        length -= seglen;
        while (seglen--)
        {
            W_MACMEM(macbase) = WifiData->txbufData[readbase++];
            macbase += 2;
        }
        if (readbase >= WIFI_TXBUFFER_SIZE / 2)
            readbase -= WIFI_TXBUFFER_SIZE / 2;
    }
    WifiData->txbufIn = readbase;

    WifiData->stats[WSTAT_TXPACKETS]++;
    WifiData->stats[WSTAT_TXBYTES] += packetlen + 12 - 4;
    WifiData->stats[WSTAT_TXDATABYTES] += packetlen - 4;

    return packetlen;
}

u16 arm7q[1024];
u16 arm7qlen = 0;

void Wifi_TxRaw(u16 *data, int datalen)
{
    datalen = (datalen + 3) & (~3);
    Wifi_MACWrite(data, 0, 0, datalen);

    // W_TXSTAT       = 0x0001;
    W_TX_RETRYLIMIT = 0x0707;
    W_TXBUF_LOC3    = 0x8000;
    W_TXREQ_SET     = 0x000D;

    WifiData->stats[WSTAT_TXPACKETS]++;
    WifiData->stats[WSTAT_TXBYTES] += datalen;
    WifiData->stats[WSTAT_TXDATABYTES] += datalen - 12;
}

static bool Wifi_TxBusy(void)
{
    if (W_TXBUSY & TXBUSY_LOC3_BUSY)
        return true;

    return false;
}

//////////////////////////////////////////////////////////////////////////
//
//  Wifi Interrupts
//

void Wifi_Intr_RxEnd(void)
{
    int oldIME = enterCriticalSection();

    int cut = 0;

    while (W_RXBUF_WRCSR != W_RXBUF_READCSR)
    {
        int base           = W_RXBUF_READCSR << 1;
        int packetlen      = Wifi_MACRead(base, 8);
        int full_packetlen = 12 + ((packetlen + 3) & (~3));

        WifiData->stats[WSTAT_RXPACKETS]++;
        WifiData->stats[WSTAT_RXBYTES] += full_packetlen;
        WifiData->stats[WSTAT_RXDATABYTES] += full_packetlen - 12;

        // process packet here
        int type = Wifi_ProcessReceivedFrame(base, full_packetlen); // returns packet type
        if (type & WifiData->reqPacketFlags || WifiData->reqReqFlags & WFLAG_REQ_PROMISC)
        {
            // If the packet type is requested (or promiscous mode is enabled),
            // forward it to the rx queue
            keepalive_time = 0;
            if (!Wifi_QueueRxMacData(base, full_packetlen))
            {
                // Failed, ignore for now.
            }
        }

        base += full_packetlen;
        if (base >= (W_RXBUF_END & 0x1FFE))
            base -= (W_RXBUF_END & 0x1FFE) - (W_RXBUF_BEGIN & 0x1FFE);
        W_RXBUF_READCSR = base >> 1;

        // Don't handle too many packets in one go
        if (cut++ > 5)
            break;
    }

    leaveCriticalSection(oldIME);
}

#define CNT_STAT_START WSTAT_HW_1B0
#define CNT_STAT_NUM   18

void Wifi_Intr_CntOverflow(void)
{
    static const u16 count_ofs_list[CNT_STAT_NUM] = {
        OFF_RXSTAT_1B0, OFF_RXSTAT_1B2, OFF_RXSTAT_1B4, OFF_RXSTAT_1B6,
        OFF_RXSTAT_1B8, OFF_RXSTAT_1BA, OFF_RXSTAT_1BC, OFF_RXSTAT_1BE,
        OFF_TX_ERR_COUNT, OFF_RX_COUNT,
        OFF_CMD_STAT_1D0, OFF_CMD_STAT_1D2, OFF_CMD_STAT_1D4, OFF_CMD_STAT_1D6,
        OFF_CMD_STAT_1D8, OFF_CMD_STAT_1DA, OFF_CMD_STAT_1DC, OFF_CMD_STAT_1DE,
    };

    int s = CNT_STAT_START;
    for (int i = 0; i < CNT_STAT_NUM; i++)
    {
        int d = WIFI_REG(count_ofs_list[i]);
        WifiData->stats[s++] += d & 0xFF;
        WifiData->stats[s++] += (d >> 8) & 0xFF;
    }
}

void Wifi_Intr_TxEnd(void)
{
    WifiData->stats[WSTAT_DEBUG] = (W_TXBUF_LOC3 & 0x8000) | (W_TXBUSY & 0x7FFF);

    if (Wifi_TxBusy())
        return;

    if (arm7qlen)
    {
        Wifi_TxRaw(arm7q, arm7qlen);
        keepalive_time = 0;
        arm7qlen       = 0;
        return;
    }

    if ((WifiData->txbufOut != WifiData->txbufIn)
        // && (!(WifiData->curReqFlags & WFLAG_REQ_APCONNECT)
        // || WifiData->authlevel == WIFI_AUTHLEVEL_ASSOCIATED)
    )
    {
        if (Wifi_CopyFirstTxData(0))
        {
            keepalive_time = 0;
            if (W_MACMEM(0x8) == 0)
            {
                // if rate dne, fill it in.
                W_MACMEM(0x8) = WifiData->maxrate7;
            }
            if (W_MACMEM(0xC) & 0x4000)
            {
                // wep is enabled, fill in the IV.
                W_MACMEM(0x24) = (W_RANDOM ^ (W_RANDOM << 7) ^ (W_RANDOM << 15)) & 0xFFFF;
                W_MACMEM(0x26) =
                    ((W_RANDOM ^ (W_RANDOM >> 7)) & 0xFF) | (WifiData->wepkeyid7 << 14);
            }
            if ((W_MACMEM(0xC) & 0x00FF) == 0x0080)
            {
                Wifi_LoadBeacon(0, 2400); // TX 0-2399, RX 0x4C00-0x5F5F
                return;
            }
            // W_TXSTAT       = 0x0001;
            W_TX_RETRYLIMIT = 0x0707;
            W_TXBUF_LOC3    = 0x8000;
            W_TXREQ_SET     = 0x000D;
        }
    }
}

void Wifi_Intr_DoNothing(void)
{
}

void Wifi_Interrupt(void)
{
    // If WiFi hasn't been initialized, don't handle any interrupt
    if (!WifiData)
    {
        W_IF = W_IF;
        return;
    }
    if (!(WifiData->flags7 & WFLAG_ARM7_RUNNING))
    {
        W_IF = W_IF;
        return;
    }

    while (1)
    {
        // First, clear the bit in the global IF register, then clear the
        // individial bits in the W_IF register.

        REG_IF = IRQ_WIFI;

        // Interrupts left to handle
        int wIF = W_IE & W_IF;
        if (wIF == 0)
            break;

        if (wIF & IRQ_RX_COMPLETE)
        {
            W_IF = IRQ_RX_COMPLETE;
            Wifi_Intr_RxEnd();
        }
        if (wIF & IRQ_TX_COMPLETE)
        {
            W_IF = IRQ_TX_COMPLETE;
            Wifi_Intr_TxEnd();
        }
        if (wIF & IRQ_RX_EVENT_INCREMENT) // RX count up
        {
            W_IF = IRQ_RX_EVENT_INCREMENT;
            Wifi_Intr_DoNothing();
        }
        if (wIF & IRQ_TX_EVENT_INCREMENT) // TX error
        {
            W_IF = IRQ_TX_EVENT_INCREMENT;
            Wifi_Intr_DoNothing();
        }
        if (wIF & IRQ_RX_EVENT_HALF_OVERFLOW) // Count Overflow
        {
            W_IF = IRQ_RX_EVENT_HALF_OVERFLOW;
            Wifi_Intr_CntOverflow();
        }
        if (wIF & IRQ_TX_ERROR_HALF_OVERFLOW) // ACK count overflow
        {
            W_IF = IRQ_TX_ERROR_HALF_OVERFLOW;
            Wifi_Intr_CntOverflow();
        }
        if (wIF & IRQ_RX_START)
        {
            W_IF = IRQ_RX_START;
            Wifi_Intr_DoNothing();
        }
        if (wIF & IRQ_TX_START)
        {
            W_IF = IRQ_TX_START;
            Wifi_Intr_DoNothing();
        }
        if (wIF & IRQ_TXBUF_COUNT_END)
        {
            W_IF = IRQ_TXBUF_COUNT_END;
            Wifi_Intr_DoNothing();
        }
        if (wIF & IRQ_RXBUF_COUNT_END)
        {
            W_IF = IRQ_RXBUF_COUNT_END;
            Wifi_Intr_DoNothing();
        }
        if (wIF & IRQ_UNUSED)
        {
            W_IF = IRQ_UNUSED;
            Wifi_Intr_DoNothing();
        }
        if (wIF & IRQ_RF_WAKEUP)
        {
            W_IF = IRQ_RF_WAKEUP;
            Wifi_Intr_DoNothing();
        }
        if (wIF & IRQ_MULTIPLAY_CMD_DONE)
        {
            W_IF = IRQ_MULTIPLAY_CMD_DONE;
            Wifi_Intr_DoNothing();
        }
        if (wIF & IRQ_POST_BEACON_TIMESLOT) // ACT End
        {
            W_IF = IRQ_POST_BEACON_TIMESLOT;
            Wifi_Intr_DoNothing();
        }
        if (wIF & IRQ_BEACON_TIMESLOT) // TBTT
        {
            W_IF = IRQ_BEACON_TIMESLOT;
            Wifi_Intr_DoNothing();
        }
        if (wIF & IRQ_PRE_BEACON_TIMESLOT) // PreTBTT
        {
            W_IF = IRQ_PRE_BEACON_TIMESLOT;
            Wifi_Intr_DoNothing();
        }
    }
}

static int scanIndex = 0;

void Wifi_Update(void)
{
    static const u8 scanlist[] = {
        1, 6, 11, 2, 3, 7, 8, 1, 6, 11, 4, 5, 9, 10, 1, 6, 11, 12, 13
    };
    static int scanlist_size = sizeof(scanlist) / sizeof(scanlist[0]);

    if (!WifiData)
        return;

    WifiData->random ^= (W_RANDOM ^ (W_RANDOM << 11) ^ (W_RANDOM << 22));
    WifiData->stats[WSTAT_ARM7_UPDATES]++;

    // check flags, to see if we need to change anything
    switch (WifiData->curMode)
    {
        case WIFIMODE_DISABLED:
            Wifi_SetLedState(LED_ALWAYS_ON);
            if (WifiData->reqMode != WIFIMODE_DISABLED)
            {
                Wifi_Start();
                WifiData->curMode = WIFIMODE_NORMAL;
            }
            break;

        case WIFIMODE_NORMAL: // main switcher function
            Wifi_SetLedState(LED_BLINK_SLOW);
            if (WifiData->reqMode == WIFIMODE_DISABLED)
            {
                Wifi_Stop();
                WifiData->curMode = WIFIMODE_DISABLED;
                break;
            }
            if (WifiData->reqMode == WIFIMODE_SCAN)
            {
                WifiData->counter7 = W_US_COUNT1; // timer hword 2 (each tick is 65.5ms)
                WifiData->curMode  = WIFIMODE_SCAN;
                break;
            }
            if (WifiData->curReqFlags & WFLAG_REQ_APCONNECT)
            {
                // already connected; disconnect
                W_BSSID[0] = WifiData->MacAddr[0];
                W_BSSID[1] = WifiData->MacAddr[1];
                W_BSSID[2] = WifiData->MacAddr[2];

                W_RXFILTER &= ~0x0400;
                W_RXFILTER |= 0x0800; // allow toDS

                W_RXFILTER2 &= ~0x0002;

                WifiData->curReqFlags &= ~WFLAG_REQ_APCONNECT;
            }
            if (WifiData->reqReqFlags & WFLAG_REQ_APCONNECT)
            {
                // not connected - connect!
                if (WifiData->reqReqFlags & WFLAG_REQ_APCOPYVALUES)
                {
                    WifiData->wepkeyid7  = WifiData->wepkeyid9;
                    WifiData->wepmode7   = WifiData->wepmode9;
                    WifiData->apchannel7 = WifiData->apchannel9;
                    Wifi_CopyMacAddr(WifiData->bssid7, WifiData->bssid9);
                    Wifi_CopyMacAddr(WifiData->apmac7, WifiData->apmac9);
                    for (int i = 0; i < 20; i++)
                        WifiData->wepkey7[i] = WifiData->wepkey9[i];
                    for (int i = 0; i < 34; i++)
                        WifiData->ssid7[i] = WifiData->ssid9[i];
                    for (int i = 0; i < 16; i++)
                        WifiData->baserates7[i] = WifiData->baserates9[i];
                    if (WifiData->reqReqFlags & WFLAG_REQ_APADHOC)
                        WifiData->curReqFlags |= WFLAG_REQ_APADHOC;
                    else
                        WifiData->curReqFlags &= ~WFLAG_REQ_APADHOC;
                }
                Wifi_SetWepKey((void *)WifiData->wepkey7);
                Wifi_SetWepMode(WifiData->wepmode7);
                // latch BSSID
                W_BSSID[0] = WifiData->bssid7[0];
                W_BSSID[1] = WifiData->bssid7[1];
                W_BSSID[2] = WifiData->bssid7[2];

                W_RXFILTER |= 0x0400;
                W_RXFILTER &= ~0x0800; // disallow toDS

                W_RXFILTER2 |= 0x0002;

                WifiData->reqChannel = WifiData->apchannel7;
                Wifi_SetChannel(WifiData->apchannel7);
                if (WifiData->curReqFlags & WFLAG_REQ_APADHOC)
                {
                    WifiData->authlevel = WIFI_AUTHLEVEL_ASSOCIATED;
                }
                else
                {
                    Wifi_SendOpenSystemAuthPacket();
                    WifiData->authlevel = WIFI_AUTHLEVEL_DISCONNECTED;
                }
                WifiData->txbufIn = WifiData->txbufOut; // empty tx buffer.
                WifiData->curReqFlags |= WFLAG_REQ_APCONNECT;
                WifiData->counter7 = W_US_COUNT1; // timer hword 2 (each tick is 65.5ms)
                WifiData->curMode  = WIFIMODE_ASSOCIATE;
                WifiData->authctr  = 0;
            }
            break;

        case WIFIMODE_SCAN:
            Wifi_SetLedState(LED_BLINK_SLOW);
            if (WifiData->reqMode != WIFIMODE_SCAN)
            {
                WifiData->curMode = WIFIMODE_NORMAL;
                break;
            }
            if (((u16)(W_US_COUNT1 - WifiData->counter7)) > 6)
            {
                // jump ship!
                WifiData->counter7   = W_US_COUNT1;
                WifiData->reqChannel = scanlist[scanIndex];
                {
                    for (int i = 0; i < WIFI_MAX_AP; i++)
                    {
                        if (WifiData->aplist[i].flags & WFLAG_APDATA_ACTIVE)
                        {
                            WifiData->aplist[i].timectr++;
                            if (WifiData->aplist[i].timectr > WIFI_AP_TIMEOUT)
                            {
                                // update rssi later.
                                WifiData->aplist[i].rssi         = 0;
                                WifiData->aplist[i].rssi_past[0] = 0;
                                WifiData->aplist[i].rssi_past[1] = 0;
                                WifiData->aplist[i].rssi_past[2] = 0;
                                WifiData->aplist[i].rssi_past[3] = 0;
                                WifiData->aplist[i].rssi_past[4] = 0;
                                WifiData->aplist[i].rssi_past[5] = 0;
                                WifiData->aplist[i].rssi_past[6] = 0;
                                WifiData->aplist[i].rssi_past[7] = 0;
                            }
                        }
                    }
                }
                scanIndex++;
                if (scanIndex == scanlist_size)
                    scanIndex = 0;
            }
            break;

        case WIFIMODE_ASSOCIATE:
            Wifi_SetLedState(LED_BLINK_SLOW);
            if (WifiData->authlevel == WIFI_AUTHLEVEL_ASSOCIATED)
            {
                WifiData->curMode = WIFIMODE_ASSOCIATED;
                break;
            }
            if (((u16)(W_US_COUNT1 - WifiData->counter7)) > 20)
            {
                // ~1 second, reattempt connect stage
                WifiData->counter7 = W_US_COUNT1;
                WifiData->authctr++;
                if (WifiData->authctr > WIFI_MAX_ASSOC_RETRY)
                {
                    WifiData->curMode = WIFIMODE_CANNOTASSOCIATE;
                    break;
                }
                switch (WifiData->authlevel)
                {
                    case WIFI_AUTHLEVEL_DISCONNECTED: // send auth packet
                        if (!(WifiData->curReqFlags & WFLAG_REQ_APADHOC))
                        {
                            Wifi_SendOpenSystemAuthPacket();
                            break;
                        }
                        WifiData->authlevel = WIFI_AUTHLEVEL_ASSOCIATED;
                        break;
                    case WIFI_AUTHLEVEL_DEASSOCIATED:
                    case WIFI_AUTHLEVEL_AUTHENTICATED: // send assoc packet
                        Wifi_SendAssocPacket();
                        break;
                    case WIFI_AUTHLEVEL_ASSOCIATED:
                        WifiData->curMode = WIFIMODE_ASSOCIATED;
                        break;
                }
            }
            if (!(WifiData->reqReqFlags & WFLAG_REQ_APCONNECT))
            {
                WifiData->curMode = WIFIMODE_NORMAL;
                break;
            }
            break;

        case WIFIMODE_ASSOCIATED:
            Wifi_SetLedState(LED_BLINK_FAST);
            keepalive_time++; // TODO: track time more accurately.
            if (keepalive_time > WIFI_KEEPALIVE_COUNT)
            {
                keepalive_time = 0;
                Wifi_SendNullFrame();
            }
            if ((u16)(W_US_COUNT1 - WifiData->pspoll_period) > WIFI_PS_POLL_CONST)
            {
                WifiData->pspoll_period = W_US_COUNT1;
                // Wifi_SendPSPollFrame();
            }
            if (!(WifiData->reqReqFlags & WFLAG_REQ_APCONNECT))
            {
                WifiData->curMode = WIFIMODE_NORMAL;
                break;
            }
            if (WifiData->authlevel != WIFI_AUTHLEVEL_ASSOCIATED)
            {
                WifiData->curMode = WIFIMODE_ASSOCIATE;
                break;
            }
            break;

        case WIFIMODE_CANNOTASSOCIATE:
            Wifi_SetLedState(LED_BLINK_SLOW);
            if (!(WifiData->reqReqFlags & WFLAG_REQ_APCONNECT))
            {
                WifiData->curMode = WIFIMODE_NORMAL;
                break;
            }
            break;
    }

    if (WifiData->curChannel != WifiData->reqChannel)
    {
        Wifi_SetChannel(WifiData->reqChannel);
    }

    // Check if we have received anything
    Wifi_Intr_RxEnd();

    // Check if we need to transfer anything
    Wifi_Intr_TxEnd();
}

//////////////////////////////////////////////////////////////////////////
//
//  Wifi User-called Functions
//
void Wifi_Init(void *wifidata)
{
    WifiData = (Wifi_MainStruct *)wifidata;

    if (isDSiMode())
    {
        // Initialize NTR WiFi on DSi.
        gpioSetWifiMode(GPIO_WIFI_MODE_NTR);
        if (REG_GPIO_WIFI)
            swiDelay(5 * 134056); // 5 milliseconds
    }

    powerOn(POWER_WIFI); // Enable power for the WiFi controller
    REG_WIFIWAITCNT =
        WIFI_RAM_N_10_CYCLES | WIFI_RAM_S_6_CYCLES | WIFI_IO_N_6_CYCLES | WIFI_IO_S_4_CYCLES;

    Wifi_FlashInitData();

    // reset/shutdown wifi:
    W_MODE_RST = 0xFFFF;
    Wifi_Stop();
    Wifi_Shutdown(); // power off wifi

    WifiData->curChannel     = 1;
    WifiData->reqChannel     = 1;
    WifiData->curMode        = WIFIMODE_DISABLED;
    WifiData->reqMode        = WIFIMODE_DISABLED;
    WifiData->reqPacketFlags = WFLAG_PACKET_ALL & (~WFLAG_PACKET_BEACON);
    WifiData->curReqFlags    = 0;
    WifiData->reqReqFlags    = 0;
    WifiData->maxrate7       = 0x0A;

    for (int i = 0; i < W_MACMEM_SIZE; i += 2)
        W_MACMEM(i) = 0;

    // load in the WFC data.
    Wifi_GetWfcSettings(WifiData);

    for (int i = 0; i < 3; i++)
        WifiData->MacAddr[i] = Wifi_FlashReadHWord(0x36 + i * 2);

    W_IE = 0;
    Wifi_WakeUp();

    Wifi_MacInit();
    Wifi_RFInit();
    Wifi_BBInit();

    // Set Default Settings
    W_MACADDR[0] = WifiData->MacAddr[0];
    W_MACADDR[1] = WifiData->MacAddr[1];
    W_MACADDR[2] = WifiData->MacAddr[2];

    W_TX_RETRYLIMIT = 7;
    Wifi_SetSleepMode(MODE_WEP_SLEEP_OFF);
    Wifi_SetWepMode(WEPMODE_NONE);

    Wifi_SetChannel(1);

    Wifi_BBWrite(REG_MM3218_CCA, 0x00);
    Wifi_BBWrite(REG_MM3218_ENERGY_DETECTION_THRESHOLD, 0x1F);

    // Wifi_Shutdown();
    WifiData->random ^= (W_RANDOM ^ (W_RANDOM << 11) ^ (W_RANDOM << 22));

    WifiData->flags7 |= WFLAG_ARM7_ACTIVE;
}

void Wifi_Deinit(void)
{
    Wifi_Stop();
    REG_POWERCNT &= ~2;
}

void Wifi_Start(void)
{
    int oldIME = enterCriticalSection();

    Wifi_Stop();

    // Wifi_WakeUp();

    W_WEP_CNT     = WEP_CNT_ENABLE;
    W_POST_BEACON = 0xFFFF;
    W_AID_FULL    = 0;
    W_AID_LOW     = 0;
    W_US_COUNTCNT = 1;
    W_POWER_TX    = 0x0000;
    W_BSSID[0]    = 0x0000;
    W_BSSID[1]    = 0x0000;
    W_BSSID[2]    = 0x0000;

    Wifi_TxSetup();
    Wifi_RxSetup();

    W_RXCNT = 0x8000;

#if 0
    switch (W_MODE_WEP & 7)
    {
        case 0: // infrastructure mode?
            W_IF = IRQ_ALL_BITS;
            W_IE = 0x003F;

            W_RXSTAT_OVF_IE  = 0x1FFF;
            // W_RXSTAT_INC_IE = 0x0400;
            W_RXFILTER       = 0xFFFF;
            W_RXFILTER2      = 0x0008;
            W_TXSTATCNT      = 0;
            W_X_00A          = 0;
            W_US_COUNTCNT    = 0;
            W_MODE_RST       = 1;
            // SetStaState(0x40);
            break;

        case 1: // ad-hoc mode? -- beacons are required to be created!
            W_IF = 0xFFF; // TODO: Is this a bug?
            W_IE = 0x703F;

            W_RXSTAT_OVF_IE  = 0x1FFF;
            W_RXSTAT_INC_IE  = 0; // 0x400
            W_RXFILTER       = 0x0301;
            W_RXFILTER2      = 0x000D;
            W_TXSTATCNT      = 0xE000;
            W_X_00A          = 0;
            W_MODE_RST       = 1;
            // ??
            W_US_COMPARECNT  = 1;
            W_TXREQ_SET      = 2;
            break;

        case 2: // DS comms mode?
#endif
    W_IF = IRQ_ALL_BITS;
    // W_IE = 0xE03F;
    W_IE = 0x40B3;

    W_RXSTAT_OVF_IE = 0x1FFF;
    W_RXSTAT_INC_IE = 0; // 0x68
    W_BSSID[0]      = WifiData->MacAddr[0];
    W_BSSID[1]      = WifiData->MacAddr[1];
    W_BSSID[2]      = WifiData->MacAddr[2];
    // W_RXFILTER      = 0xEFFF;
    // W_RXFILTER2     = 0x0008;
    W_RXFILTER       = 0x0981; // 0x0181
    W_RXFILTER2      = 0x0009; // 0x000B
    W_TXSTATCNT      = 0;
    W_X_00A          = 0;
    W_MODE_RST       = 1;
    W_US_COUNTCNT    = 1;
    W_US_COMPARECNT  = 1;
    // SetStaState(0x20);
#if 0
            break;

        case 3:
        case 4:
            break;
    }
#endif
    W_POWER_48 = 0;
    Wifi_DisableTempPowerSave();
    // W_TXREQ_SET = 0x0002;
    W_POWERSTATE |= 2;
    W_TXREQ_RESET = 0xFFFF;

    int i = 0xFA0;
    while (i != 0 && !(W_RF_PINS & 0x80))
        i--;

    WifiData->flags7 |= WFLAG_ARM7_RUNNING;

    leaveCriticalSection(oldIME);
}

void Wifi_Stop(void)
{
    int oldIME = enterCriticalSection();

    WifiData->flags7 &= ~WFLAG_ARM7_RUNNING;

    W_IE            = 0;
    W_MODE_RST      = 0;
    W_US_COMPARECNT = 0;
    W_US_COUNTCNT   = 0;
    W_TXSTATCNT     = 0;
    W_X_00A         = 0;
    W_TXBUF_BEACON  = 0;
    W_TXREQ_RESET   = 0xFFFF;
    W_TXBUF_RESET   = 0xFFFF;

    // Wifi_Shutdown();

    leaveCriticalSection(oldIME);
}

void Wifi_SetWepKey(void *wepkey)
{
    for (int i = 0; i < 16; i++)
    {
        W_WEPKEY_0[i] = ((u16 *)wepkey)[i];
        W_WEPKEY_1[i] = ((u16 *)wepkey)[i];
        W_WEPKEY_2[i] = ((u16 *)wepkey)[i];
        W_WEPKEY_3[i] = ((u16 *)wepkey)[i];
    }
}

void Wifi_SetWepMode(int wepmode)
{
    if (wepmode < 0 || wepmode > 7)
        return;

    if (wepmode == WEPMODE_NONE)
        W_WEP_CNT = WEP_CNT_DISABLE;
    else
        W_WEP_CNT = WEP_CNT_ENABLE;

    if (wepmode == WEPMODE_NONE)
        wepmode = WEPMODE_40BIT;

    W_MODE_WEP = (W_MODE_WEP & ~MODE_WEP_KEYLEN_MASK)
               | (wepmode << MODE_WEP_KEYLEN_SHIFT);
}

void Wifi_SetSleepMode(int mode)
{
    if (mode > 3 || mode < 0)
        return;

    W_MODE_WEP = (W_MODE_WEP & ~MODE_WEP_SLEEP_MASK) | mode;
}

void Wifi_SetPreambleType(int preamble_type)
{
    if (preamble_type > 1 || preamble_type < 0)
        return;

    W_PREAMBLE = (W_PREAMBLE & 0xFFBF) | (preamble_type << 6);
}

void Wifi_DisableTempPowerSave(void)
{
    W_POWER_TX &= ~2;
    W_POWER_48 = 0;
}

//////////////////////////////////////////////////////////////////////////
//
//  802.11b system, tied in a bit with the :

int Wifi_TxQueue(u16 *data, int datalen)
{
    if (arm7qlen)
    {
        if (!Wifi_TxBusy())
        {
            Wifi_TxRaw(arm7q, arm7qlen);
            arm7qlen = 0;

            int j = (datalen + 1) >> 1;
            if (j > 1024)
                return 0;

            for (int i = 0; i < j; i++)
                arm7q[i] = data[i];

            arm7qlen = datalen;
            return 1;
        }
        return 0;
    }
    if (!Wifi_TxBusy())
    {
        Wifi_TxRaw(data, datalen);
        return 1;
    }

    arm7qlen = 0;

    int j = (datalen + 1) >> 1;
    if (j > 1024)
        return 0;

    for (int i = 0; i < j; i++)
        arm7q[i] = data[i];

    arm7qlen = datalen;
    return 1;
}

int Wifi_GenMgtHeader(u8 *data, u16 headerflags)
{
    // tx header
    ((u16 *)data)[0] = 0;
    ((u16 *)data)[1] = 0;
    ((u16 *)data)[2] = 0;
    ((u16 *)data)[3] = 0;
    ((u16 *)data)[4] = 0;
    ((u16 *)data)[5] = 0;
    // fill in most header fields
    ((u16 *)data)[7] = 0x0000;
    Wifi_CopyMacAddr(data + 16, WifiData->apmac7);
    Wifi_CopyMacAddr(data + 22, WifiData->MacAddr);
    Wifi_CopyMacAddr(data + 28, WifiData->bssid7);
    ((u16 *)data)[17] = 0;

    // fill in wep-specific stuff
    if (headerflags & 0x4000)
    {
        // I'm lazy and certainly haven't done this to spec.
        ((u32 *)data)[9] = ((W_RANDOM ^ (W_RANDOM << 7) ^ (W_RANDOM << 15)) & 0x0FFF)
                           | (WifiData->wepkeyid7 << 30);
        ((u16 *)data)[6] = headerflags;
        return 28 + 12;
    }
    else
    {
        ((u16 *)data)[6] = headerflags;
        return 24 + 12;
    }
}

int Wifi_SendOpenSystemAuthPacket(void)
{
    // max size is 12+24+4+6 = 46
    u8 data[64];
    int i = Wifi_GenMgtHeader(data, 0x00B0);

    ((u16 *)(data + i))[0] = 0; // Authentication algorithm number (0=open system)
    ((u16 *)(data + i))[1] = 1; // Authentication sequence number
    ((u16 *)(data + i))[2] = 0; // Authentication status code (reserved for this message, =0)

    ((u16 *)data)[4] = 0x000A;
    ((u16 *)data)[5] = i + 6 - 12 + 4;

    return Wifi_TxQueue((u16 *)data, i + 6);
}

int Wifi_SendSharedKeyAuthPacket(void)
{
    // max size is 12+24+4+6 = 46
    u8 data[64];
    int i = Wifi_GenMgtHeader(data, 0x00B0);

    ((u16 *)(data + i))[0] = 1; // Authentication algorithm number (1=shared key)
    ((u16 *)(data + i))[1] = 1; // Authentication sequence number
    ((u16 *)(data + i))[2] = 0; // Authentication status code (reserved for this message, =0)

    ((u16 *)data)[4] = 0x000A;
    ((u16 *)data)[5] = i + 6 - 12 + 4;

    return Wifi_TxQueue((u16 *)data, i + 6);
}

int Wifi_SendSharedKeyAuthPacket2(int challenge_length, u8 *challenge_Text)
{
    u8 data[320];
    int i = Wifi_GenMgtHeader(data, 0x40B0);

    ((u16 *)(data + i))[0] = 1; // Authentication algorithm number (1=shared key)
    ((u16 *)(data + i))[1] = 3; // Authentication sequence number
    ((u16 *)(data + i))[2] = 0; // Authentication status code (reserved for this message, =0)

    data[i + 6] = 0x10; // 16=challenge text block
    data[i + 7] = challenge_length;

    for (int j = 0; j < challenge_length; j++)
        data[i + j + 8] = challenge_Text[j];

    ((u16 *)data)[4] = 0x000A;
    ((u16 *)data)[5] = i + 8 + challenge_length - 12 + 4 + 4;

    return Wifi_TxQueue((u16 *)data, i + 8 + challenge_length);
}

// uses arm7 data in our struct
int Wifi_SendAssocPacket(void)
{
    u8 data[96];
    int j, numrates;

    int i = Wifi_GenMgtHeader(data, 0x0000);

    if (WifiData->wepmode7)
    {
        ((u16 *)(data + i))[0] = 0x0031; // CAPS info
    }
    else
    {
        ((u16 *)(data + i))[0] = 0x0021; // CAPS info
    }

    ((u16 *)(data + i))[1] = W_LISTENINT; // Listen interval
    i += 4;
    data[i++] = 0; // SSID element
    data[i++] = WifiData->ssid7[0];
    for (j = 0; j < WifiData->ssid7[0]; j++)
        data[i++] = WifiData->ssid7[1 + j];

    if ((WifiData->baserates7[0] & 0x7f) != 2)
    {
        for (j = 1; j < 16; j++)
            WifiData->baserates7[j] = WifiData->baserates7[j - 1];
    }
    WifiData->baserates7[0] = 0x82;
    if ((WifiData->baserates7[1] & 0x7f) != 4)
    {
        for (j = 2; j < 16; j++)
            WifiData->baserates7[j] = WifiData->baserates7[j - 1];
    }
    WifiData->baserates7[1] = 0x04;

    WifiData->baserates7[15] = 0;
    for (j = 0; j < 16; j++)
        if (WifiData->baserates7[j] == 0)
            break;
    numrates = j;
    for (j = 2; j < numrates; j++)
        WifiData->baserates7[j] &= 0x7F;

    data[i++] = 1; // rate set
    data[i++] = numrates;
    for (j = 0; j < numrates; j++)
        data[i++] = WifiData->baserates7[j];

    // reset header fields with needed data
    ((u16 *)data)[4] = 0x000A;
    ((u16 *)data)[5] = i - 12 + 4;

    return Wifi_TxQueue((u16 *)data, i);
}

// Fix: Either sent ToDS properly or drop ToDS flag. Also fix length (16+4)
int Wifi_SendNullFrame(void)
{
    // max size is 12+16 = 28
    u16 data[16];
    // tx header
    data[0] = 0;
    data[1] = 0;
    data[2] = 0;
    data[3] = 0;
    data[4] = WifiData->maxrate7;
    data[5] = 18 + 4;
    // fill in packet header fields
    data[6] = 0x0148;
    data[7] = 0;
    Wifi_CopyMacAddr(&data[8], WifiData->apmac7);
    Wifi_CopyMacAddr(&data[11], WifiData->MacAddr);

    return Wifi_TxQueue(data, 30);
}

int Wifi_SendPSPollFrame(void)
{
    // max size is 12+16 = 28
    u16 data[16];
    // tx header
    data[0] = 0;
    data[1] = 0;
    data[2] = 0;
    data[3] = 0;
    data[4] = WifiData->maxrate7;
    data[5] = 16 + 4;
    // fill in packet header fields
    data[6] = 0x01A4;
    data[7] = 0xC000 | W_AID_LOW;
    Wifi_CopyMacAddr(&data[8], WifiData->apmac7);
    Wifi_CopyMacAddr(&data[11], WifiData->MacAddr);

    return Wifi_TxQueue(data, 28);
}

int Wifi_ProcessReceivedFrame(int macbase, int framelen)
{
    (void)framelen;

    Wifi_RxHeader packetheader;
    u16 control_802;
    Wifi_MACCopy((u16 *)&packetheader, macbase, 0, 12);
    control_802 = Wifi_MACRead(macbase, 12);

    switch ((control_802 >> 2) & 0x3F)
    {
        // Management Frames
        case 0x20: // 1000 00 Beacon
        case 0x14: // 0101 00 Probe Response // process probe responses too.
            // mine data from the beacon...
            {
                u8 data[512];
                u8 wepmode, fromsta;
                u8 segtype, seglen;
                u8 channel;
                u8 wpamode;
                u8 rateset[16];
                u16 ptr_ssid;
                u16 maxrate;
                u16 curloc;
                u32 datalen;
                u16 i, j, compatible;
                u16 num_uni_ciphers;

                datalen = packetheader.byteLength;
                if (datalen > 512)
                    datalen = 512;
                Wifi_MACCopy((u16 *)data, macbase, 12, (datalen + 1) & ~1);
                wepmode = 0;
                maxrate = 0;
                if (((u16 *)data)[5 + 12] & 0x0010)
                {
                    // capability info, WEP bit
                    wepmode = 1;
                }
                fromsta    = Wifi_CmpMacAddr(data + 10, data + 16);
                curloc     = 12 + 24; // 12 fixed bytes, 24 802.11 header
                compatible = 1;
                ptr_ssid   = 0;
                channel    = WifiData->curChannel;
                wpamode    = 0;
                rateset[0] = 0;

                do
                {
                    if (curloc >= datalen)
                        break;
                    segtype = data[curloc++];
                    seglen  = data[curloc++];

                    switch (segtype)
                    {
                        case 0: // SSID element
                            ptr_ssid = curloc - 2;
                            break;
                        case 1: // rate set (make sure we're compatible)
                            compatible = 0;
                            maxrate    = 0;
                            j          = 0;
                            for (i = 0; i < seglen; i++)
                            {
                                if ((data[curloc + i] & 0x7F) > maxrate)
                                    maxrate = data[curloc + i] & 0x7F;
                                if (j < 15 && data[curloc + i] & 0x80)
                                    rateset[j++] = data[curloc + i];
                            }
                            for (i = 0; i < seglen; i++)
                            {
                                if (data[curloc + i] == 0x82 || data[curloc + i] == 0x84)
                                    compatible = 1; // 1-2mbit, fully compatible
                                else if (data[curloc + i] == 0x8B || data[curloc + i] == 0x96)
                                    compatible = 2; // 5.5,11mbit, have to fake our way in.
                                else if (data[curloc + i] & 0x80)
                                {
                                    compatible = 0;
                                    break;
                                }
                            }
                            rateset[j] = 0;
                            break;
                        case 3: // DS set (current channel)
                            channel = data[curloc];
                            break;
                        case 48: // RSN(A) field- WPA enabled.
                            j = curloc;
                            if (seglen >= 10 && data[j] == 0x01 && data[j + 1] == 0x00)
                            {
                                j += 6; // Skip multicast
                                num_uni_ciphers = data[j] + (data[j + 1] << 8);
                                j += 2;
                                while (num_uni_ciphers-- && (j <= (curloc + seglen - 4)))
                                {
                                    // check first 3 bytes
                                    if (data[j] == 0x00 && data[j + 1] == 0x0f
                                        && data[j + 2] == 0xAC)
                                    {
                                        j += 3;
                                        switch (data[j++])
                                        {
                                            case 2: // TKIP
                                            case 3: // AES WRAP
                                            case 4: // AES CCMP
                                                wpamode = 1;
                                                break;
                                            case 1: // WEP64
                                            case 5: // WEP128
                                                wepmode = 1;
                                                break;
                                                // others : 0:NONE
                                        }
                                    }
                                }
                            }
                            break;
                        case 221: // vendor specific;
                            j = curloc;
                            if (seglen >= 14 && data[j] == 0x00 && data[j + 1] == 0x50
                                && data[j + 2] == 0xF2 && data[j + 3] == 0x01 && data[j + 4] == 0x01
                                && data[j + 5] == 0x00)
                            {
                                // WPA IE type 1 version 1
                                // Skip multicast cipher suite
                                j += 10;
                                num_uni_ciphers = data[j] + (data[j + 1] << 8);
                                j += 2;
                                while (num_uni_ciphers-- && j <= (curloc + seglen - 4))
                                {
                                    // check first 3 bytes
                                    if (data[j] == 0x00 && data[j + 1] == 0x50
                                        && data[j + 2] == 0xF2)
                                    {
                                        j += 3;
                                        switch (data[j++])
                                        {
                                            case 2: // TKIP
                                            case 3: // AES WRAP
                                            case 4: // AES CCMP
                                                wpamode = 1;
                                                break;
                                            case 1: // WEP64
                                            case 5: // WEP128
                                                wepmode = 1;
                                                // others : 0:NONE
                                        }
                                    }
                                }
                            }
                            break;
                    }
                    // don't care about the others.

                    curloc += seglen;
                } while (curloc < datalen);

                if (wpamode == 1)
                    compatible = 0;

                seglen  = 0;
                segtype = 255;
                for (i = 0; i < WIFI_MAX_AP; i++)
                {
                    if (Wifi_CmpMacAddr(WifiData->aplist[i].bssid, data + 16))
                    {
                        seglen++;
                        if (Spinlock_Acquire(WifiData->aplist[i]) == SPINLOCK_OK)
                        {
                            WifiData->aplist[i].timectr = 0;
                            WifiData->aplist[i].flags   = WFLAG_APDATA_ACTIVE
                                                        | (wepmode ? WFLAG_APDATA_WEP : 0)
                                                        | (fromsta ? 0 : WFLAG_APDATA_ADHOC);
                            if (compatible == 1)
                                WifiData->aplist[i].flags |= WFLAG_APDATA_COMPATIBLE;
                            if (compatible == 2)
                                WifiData->aplist[i].flags |= WFLAG_APDATA_EXTCOMPATIBLE;
                            if (wpamode == 1)
                                WifiData->aplist[i].flags |= WFLAG_APDATA_WPA;
                            WifiData->aplist[i].maxrate = maxrate;

                            // src: +10
                            Wifi_CopyMacAddr(WifiData->aplist[i].macaddr, data + 10);
                            if (ptr_ssid)
                            {
                                WifiData->aplist[i].ssid_len = data[ptr_ssid + 1];
                                if (WifiData->aplist[i].ssid_len > 32)
                                    WifiData->aplist[i].ssid_len = 32;
                                for (j = 0; j < WifiData->aplist[i].ssid_len; j++)
                                {
                                    WifiData->aplist[i].ssid[j] = data[ptr_ssid + 2 + j];
                                }
                                WifiData->aplist[i].ssid[j] = 0;
                            }
                            if (WifiData->curChannel == channel)
                            {
                                // only use RSSI when we're on the right channel
                                if (WifiData->aplist[i].rssi_past[0] == 0)
                                {
                                    // min rssi is 2, heh.
                                    int tmp = packetheader.rssi_ & 255;

                                    WifiData->aplist[i].rssi_past[0] = tmp;
                                    WifiData->aplist[i].rssi_past[1] = tmp;
                                    WifiData->aplist[i].rssi_past[2] = tmp;
                                    WifiData->aplist[i].rssi_past[3] = tmp;
                                    WifiData->aplist[i].rssi_past[4] = tmp;
                                    WifiData->aplist[i].rssi_past[5] = tmp;
                                    WifiData->aplist[i].rssi_past[6] = tmp;
                                    WifiData->aplist[i].rssi_past[7] = tmp;
                                }
                                else
                                {
                                    for (j = 0; j < 7; j++)
                                    {
                                        WifiData->aplist[i].rssi_past[j] =
                                            WifiData->aplist[i].rssi_past[j + 1];
                                    }
                                    WifiData->aplist[i].rssi_past[7] = packetheader.rssi_ & 255;
                                }
                            }
                            WifiData->aplist[i].channel = channel;

                            for (j = 0; j < 16; j++)
                                WifiData->aplist[i].base_rates[j] = rateset[j];
                            Spinlock_Release(WifiData->aplist[i]);
                        }
                        else
                        {
                            // couldn't update beacon - oh well :\ there'll be other beacons.
                        }
                    }
                    else
                    {
                        if (WifiData->aplist[i].flags & WFLAG_APDATA_ACTIVE)
                        {
                            // WifiData->aplist[i].timectr++;
                        }
                        else
                        {
                            if (segtype == 255)
                                segtype = i;
                        }
                    }
                }
                if (seglen == 0)
                {
                    // we couldn't find an existing record
                    if (segtype == 255)
                    {
                        j = 0;

                        segtype = 0; // prevent heap corruption if wifilib detects >WIFI_MAX_AP
                                     // APs before entering scan mode.
                        for (i = 0; i < WIFI_MAX_AP; i++)
                        {
                            if (WifiData->aplist[i].timectr > j)
                            {
                                j = WifiData->aplist[i].timectr;

                                segtype = i;
                            }
                        }
                    }
                    // stuff new data in
                    i = segtype;
                    if (Spinlock_Acquire(WifiData->aplist[i]) == SPINLOCK_OK)
                    {
                        Wifi_CopyMacAddr(WifiData->aplist[i].bssid, data + 16);   // bssid: +16
                        Wifi_CopyMacAddr(WifiData->aplist[i].macaddr, data + 10); // src: +10
                        WifiData->aplist[i].timectr = 0;
                        WifiData->aplist[i].flags   = WFLAG_APDATA_ACTIVE
                                                    | (wepmode ? WFLAG_APDATA_WEP : 0)
                                                    | (fromsta ? 0 : WFLAG_APDATA_ADHOC);
                        if (compatible == 1)
                            WifiData->aplist[i].flags |= WFLAG_APDATA_COMPATIBLE;
                        if (compatible == 2)
                            WifiData->aplist[i].flags |= WFLAG_APDATA_EXTCOMPATIBLE;
                        if (wpamode == 1)
                            WifiData->aplist[i].flags |= WFLAG_APDATA_WPA;
                        WifiData->aplist[i].maxrate = maxrate;

                        if (ptr_ssid)
                        {
                            WifiData->aplist[i].ssid_len = data[ptr_ssid + 1];
                            if (WifiData->aplist[i].ssid_len > 32)
                                WifiData->aplist[i].ssid_len = 32;
                            for (j = 0; j < WifiData->aplist[i].ssid_len; j++)
                            {
                                WifiData->aplist[i].ssid[j] = data[ptr_ssid + 2 + j];
                            }
                            WifiData->aplist[i].ssid[j] = 0;
                        }
                        else
                        {
                            WifiData->aplist[i].ssid[0]  = 0;
                            WifiData->aplist[i].ssid_len = 0;
                        }

                        if (WifiData->curChannel == channel)
                        {
                            // only use RSSI when we're on the right channel
                            int tmp = packetheader.rssi_ & 255;

                            WifiData->aplist[i].rssi_past[0] = tmp;
                            WifiData->aplist[i].rssi_past[1] = tmp;
                            WifiData->aplist[i].rssi_past[2] = tmp;
                            WifiData->aplist[i].rssi_past[3] = tmp;
                            WifiData->aplist[i].rssi_past[4] = tmp;
                            WifiData->aplist[i].rssi_past[5] = tmp;
                            WifiData->aplist[i].rssi_past[6] = tmp;
                            WifiData->aplist[i].rssi_past[7] = tmp;
                        }
                        else
                        {
                            // update rssi later.
                            WifiData->aplist[i].rssi_past[0] = 0;
                            WifiData->aplist[i].rssi_past[1] = 0;
                            WifiData->aplist[i].rssi_past[2] = 0;
                            WifiData->aplist[i].rssi_past[3] = 0;
                            WifiData->aplist[i].rssi_past[4] = 0;
                            WifiData->aplist[i].rssi_past[5] = 0;
                            WifiData->aplist[i].rssi_past[6] = 0;
                            WifiData->aplist[i].rssi_past[7] = 0;
                        }

                        WifiData->aplist[i].channel = channel;
                        for (j = 0; j < 16; j++)
                            WifiData->aplist[i].base_rates[j] = rateset[j];

                        Spinlock_Release(WifiData->aplist[i]);
                    }
                    else
                    {
                        // couldn't update beacon - oh well :\ there'll be other beacons.
                    }
                }
            }
            if (((control_802 >> 2) & 0x3F) == 0x14)
                return WFLAG_PACKET_MGT;
            return WFLAG_PACKET_BEACON;

        case 0x04: // 0001 00 Assoc Response
        case 0x0C: // 0011 00 Reassoc Response
            // we might have been associated, let's check.
            {
                int datalen, i;
                u8 data[64];
                datalen = packetheader.byteLength;
                if (datalen > 64)
                    datalen = 64;
                Wifi_MACCopy((u16 *)data, macbase, 12, (datalen + 1) & ~1);

                if (Wifi_CmpMacAddr(data + 4, WifiData->MacAddr))
                {
                    // packet is indeed sent to us.
                    if (Wifi_CmpMacAddr(data + 16, WifiData->bssid7))
                    {
                        // packet is indeed from the base station we're trying to associate to.
                        if (((u16 *)(data + 24))[1] == 0)
                        {
                            // status code, 0==success
                            W_AID_LOW  = ((u16 *)(data + 24))[2];
                            W_AID_FULL = ((u16 *)(data + 24))[2];

                            // set max rate
                            WifiData->maxrate7 = 0xA;
                            for (i = 0; i < ((u8 *)(data + 24))[7]; i++)
                            {
                                if (((u8 *)(data + 24))[8 + i] == 0x84
                                    || ((u8 *)(data + 24))[8 + i] == 0x04)
                                {
                                    WifiData->maxrate7 = 0x14;
                                }
                            }
                            if (WifiData->authlevel == WIFI_AUTHLEVEL_AUTHENTICATED
                                || WifiData->authlevel == WIFI_AUTHLEVEL_DEASSOCIATED)
                            {
                                WifiData->authlevel = WIFI_AUTHLEVEL_ASSOCIATED;
                                WifiData->authctr   = 0;
                            }
                        }
                        else
                        { // status code = failure!
                            WifiData->curMode = WIFIMODE_CANNOTASSOCIATE;
                        }
                    }
                }
            }
            return WFLAG_PACKET_MGT;

        case 0x00: // 0000 00 Assoc Request
        case 0x08: // 0010 00 Reassoc Request
        case 0x10: // 0100 00 Probe Request
        case 0x24: // 1001 00 ATIM
        case 0x28: // 1010 00 Disassociation
            return WFLAG_PACKET_MGT;

        case 0x2C: // 1011 00 Authentication
            // check auth response to ensure we're in
            {
                int datalen;
                u8 data[384];
                datalen = packetheader.byteLength;
                if (datalen > 384)
                    datalen = 384;
                Wifi_MACCopy((u16 *)data, macbase, 12, (datalen + 1) & ~1);

                if (Wifi_CmpMacAddr(data + 4, WifiData->MacAddr))
                {
                    // packet is indeed sent to us.
                    if (Wifi_CmpMacAddr(data + 16, WifiData->bssid7))
                    {
                        // packet is indeed from the base station we're trying to associate to.
                        if (((u16 *)(data + 24))[0] == 0)
                        {
                            // open system auth
                            if (((u16 *)(data + 24))[1] == 2)
                            {
                                // seq 2, should be final sequence
                                if (((u16 *)(data + 24))[2] == 0)
                                {
                                    // status code: successful
                                    if (WifiData->authlevel == WIFI_AUTHLEVEL_DISCONNECTED)
                                    {
                                        WifiData->authlevel = WIFI_AUTHLEVEL_AUTHENTICATED;
                                        WifiData->authctr   = 0;
                                        Wifi_SendAssocPacket();
                                    }
                                }
                                else
                                {
                                    // status code: rejected, try something else
                                    Wifi_SendSharedKeyAuthPacket();
                                }
                            }
                        }
                        else if (((u16 *)(data + 24))[0] == 1)
                        {
                            // shared key auth
                            if (((u16 *)(data + 24))[1] == 2)
                            {
                                // seq 2, challenge text
                                if (((u16 *)(data + 24))[2] == 0)
                                {
                                    // status code: successful
                                    // scrape challenge text and send challenge reply
                                    if (data[24 + 6] == 0x10)
                                    {
                                        // 16 = challenge text - this value must be 0x10 or else!
                                        Wifi_SendSharedKeyAuthPacket2(data[24 + 7], data + 24 + 8);
                                    }
                                }
                                else
                                {
                                    // rejected, just give up.
                                    WifiData->curMode = WIFIMODE_CANNOTASSOCIATE;
                                }
                            }
                            else if (((u16 *)(data + 24))[1] == 4)
                            {
                                // seq 4, accept/deny
                                if (((u16 *)(data + 24))[2] == 0)
                                {
                                    // status code: successful
                                    if (WifiData->authlevel == WIFI_AUTHLEVEL_DISCONNECTED)
                                    {
                                        WifiData->authlevel = WIFI_AUTHLEVEL_AUTHENTICATED;
                                        WifiData->authctr   = 0;
                                        Wifi_SendAssocPacket();
                                    }
                                }
                                else
                                {
                                    // status code: rejected. Cry in the corner.
                                    WifiData->curMode = WIFIMODE_CANNOTASSOCIATE;
                                }
                            }
                        }
                    }
                }
            }
            return WFLAG_PACKET_MGT;

        case 0x30: // 1100 00 Deauthentication
            {
                int datalen;
                u8 data[64];
                datalen = packetheader.byteLength;
                if (datalen > 64)
                    datalen = 64;
                Wifi_MACCopy((u16 *)data, macbase, 12, (datalen + 1) & ~1);

                if (Wifi_CmpMacAddr(data + 4, WifiData->MacAddr))
                {
                    // packet is indeed sent to us.
                    if (Wifi_CmpMacAddr(data + 16, WifiData->bssid7))
                    {
                        // packet is indeed from the base station we're trying to associate to.

                        // bad things! they booted us!.
                        // back to square 1.
                        if (WifiData->curReqFlags & WFLAG_REQ_APADHOC)
                        {
                            WifiData->authlevel = WIFI_AUTHLEVEL_AUTHENTICATED;
                            Wifi_SendAssocPacket();
                        }
                        else
                        {
                            WifiData->authlevel = WIFI_AUTHLEVEL_DISCONNECTED;
                            Wifi_SendOpenSystemAuthPacket();
                        }
                    }
                }
            }
            return WFLAG_PACKET_MGT;

            // Control Frames
        case 0x29: // 1010 01 PowerSave Poll
        case 0x2D: // 1011 01 RTS
        case 0x31: // 1100 01 CTS
        case 0x35: // 1101 01 ACK
        case 0x39: // 1110 01 CF-End
        case 0x3D: // 1111 01 CF-End+CF-Ack
            return WFLAG_PACKET_CTRL;

            // Data Frames
        case 0x02: // 0000 10 Data
        case 0x06: // 0001 10 Data + CF-Ack
        case 0x0A: // 0010 10 Data + CF-Poll
        case 0x0E: // 0011 10 Data + CF-Ack + CF-Poll
            // We like data!
            return WFLAG_PACKET_DATA;

        case 0x12: // 0100 10 Null Function
        case 0x16: // 0101 10 CF-Ack
        case 0x1A: // 0110 10 CF-Poll
        case 0x1E: // 0111 10 CF-Ack + CF-Poll
            return WFLAG_PACKET_DATA;

        default: // ignore!
            return 0;
    }
}
