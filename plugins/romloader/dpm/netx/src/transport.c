/***************************************************************************
 *   Copyright (C) 2011 by Christoph Thelen                                *
 *   doc_bacardi@users.sourceforge.net                                     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/


#include "transport.h"

#include <stddef.h>
#include <stdint.h>


#include "monitor.h"
#include "monitor_commands.h"
#include "serial_vectors.h"
#include "systime.h"


/*-------------------------------------------------------------------------*/

#define NETX56_DPM_BOOT_NETX_RECEIVED_CMD      0x01
#define NETX56_DPM_BOOT_NETX_SEND_CMD          0x02

#define NETX56_DPM_BOOT_HOST_SEND_CMD          0x01
#define NETX56_DPM_BOOT_HOST_RECEIVED_CMD      0x02

#define SRT_NETX56_HANDSHAKE_REG_ARM_DATA      16
#define SRT_NETX56_HANDSHAKE_REG_PC_DATA       24

/* The ID "MMON" shows that the monitor is running. */
#define BOOT_ID_MONITOR 0x4e4f4d4d

#define NETX56_DPM_NETX_TO_HOST_BUFFERSIZE     0x0200
#define NETX56_DPM_HOST_TO_NETX_BUFFERSIZE     0x0400

typedef struct HBOOT_DPM_NETX56_STRUCT
{
	volatile uint32_t ulDpmBootId;
	volatile uint32_t ulDpmByteSize;
	volatile uint32_t ulSdramExtGeneralCtrl;
	volatile uint32_t ulSdramExtTimingCtrl;
	volatile uint32_t ulSdramExtByteSize;
	volatile uint32_t ulSdramHifGeneralCtrl;
	volatile uint32_t ulSdramHifTimingCtrl;
	volatile uint32_t ulSdramHifByteSize;
	volatile uint32_t aulReserved_20[22];
	volatile uint32_t ulNetxToHostDataSize;
	volatile uint32_t ulHostToNetxDataSize;
	volatile uint32_t ulHandshake;
	volatile uint32_t aulReserved_84[31];
	volatile uint8_t aucNetxToHostData[NETX56_DPM_NETX_TO_HOST_BUFFERSIZE];
	volatile uint8_t aucHostToNetxData[NETX56_DPM_HOST_TO_NETX_BUFFERSIZE];
} HBOOT_DPM_NETX56_T;

static HBOOT_DPM_NETX56_T tDpm __attribute__ ((section (".dpm")));


/*-------------------------------------------------------------------------*/

size_t sizStreamBufferHead;
size_t sizStreamBufferFill;

size_t sizPacketOutputFill;
size_t sizPacketOutputFillLast;


/*-------------------------------------------------------------------------*/


static void dpm_init(void)
{
	HOSTDEF(ptDpmArea);
	unsigned long ulValue;
	unsigned long ulNetxAdr;


	/* Setup the DPM at the new location. */
	tDpm.ulDpmBootId = BOOT_ID_MONITOR;
	tDpm.ulDpmByteSize = 0x0100 + sizeof(HBOOT_DPM_NETX56_T);
	tDpm.ulSdramExtGeneralCtrl = 0;
	tDpm.ulSdramExtTimingCtrl = 0;
	tDpm.ulSdramExtByteSize = 0;
	tDpm.ulSdramHifGeneralCtrl = 0;
	tDpm.ulSdramHifTimingCtrl = 0;
	tDpm.ulSdramHifByteSize = 0;
	tDpm.ulNetxToHostDataSize = 0;
	tDpm.ulHostToNetxDataSize = 0;
	tDpm.ulHandshake = 0;


	/* Setup the map for the new DPM location. */
        /* DPM mapping:
         * 0x0000 - 0x00ff : DPM configuration area
         * 0x0100 - 0x017f : dpm_mb_start
         * 0x0180 - 0x01ff : intramhs_dpm_mirror
         * 0x0200 - 0x07ff : dpm_mb_start+0x0100
         */
	ptDpmArea->asDpm_win[0].ulEnd = 0x017fU + 1;
	ulNetxAdr = (unsigned long)&tDpm;
	ulValue  = (ulNetxAdr-0x0100U) & HOSTMSK(dpm_win1_map_win_map);
	ulValue |= ulNetxAdr & HOSTMSK(dpm_win1_map_win_page);
	ulValue |= HOSTMSK(dpm_win1_map_byte_area)|HOSTMSK(dpm_win1_map_read_ahead);
	ptDpmArea->asDpm_win[0].ulMap = ulValue;

	ptDpmArea->asDpm_win[1].ulEnd = 0x01ffU + 1;
	ulNetxAdr = HOSTADDR(intramhs_dpm_mirror);
	ulValue  = (ulNetxAdr-0x0180U) & HOSTMSK(dpm_win1_map_win_map);
	ulValue |= ulNetxAdr & HOSTMSK(dpm_win1_map_win_page);
	ulValue |= HOSTMSK(dpm_win1_map_byte_area);
	ptDpmArea->asDpm_win[1].ulMap = ulValue;

	ptDpmArea->asDpm_win[2].ulEnd = 0x07ffU + 1;
	ulNetxAdr = ((unsigned long)&tDpm)+0x100;
	ulValue  = (ulNetxAdr-0x0200U) & HOSTMSK(dpm_win1_map_win_map);
	ulValue |= ulNetxAdr & HOSTMSK(dpm_win1_map_win_page);
	ulValue |= HOSTMSK(dpm_win1_map_byte_area)|HOSTMSK(dpm_win1_map_read_ahead);
	ptDpmArea->asDpm_win[2].ulMap = ulValue;

	ptDpmArea->asDpm_win[3].ulEnd = 0U;
	ptDpmArea->asDpm_win[3].ulMap = 0U;

}



static unsigned long mailbox_purr(unsigned long ulMask)
{
	HOSTDEF(ptHandshakeDtcmArmMirrorArea);
	unsigned long ulValue;
	unsigned long ulHostPart;
	unsigned long ulNetxPart;


	ulValue = ptHandshakeDtcmArmMirrorArea->aulHandshakeReg[0];

	ulHostPart  = ulValue >> SRT_NETX56_HANDSHAKE_REG_PC_DATA;
	ulHostPart &= 3;
	ulNetxPart  = ulValue >> SRT_NETX56_HANDSHAKE_REG_ARM_DATA;
	ulNetxPart &= 3;

	/* Check for harmony. */
	ulValue  = ulHostPart ^ ulNetxPart;
	/* Mask */
	ulValue &= ulMask;

	return ulValue;
}


/*-------------------------------------------------------------------------*/

void transport_init(void)
{
	/* Initialize the DPM. */
	dpm_init();

	monitor_init();

	/* Initialize the stream buffer. */
	sizStreamBufferHead = 0;
	sizStreamBufferFill = 0;

	sizPacketOutputFill = 0;
	sizPacketOutputFillLast = 0;
}



void transport_loop(void)
{
	unsigned long ulValue;
	unsigned long ulPacketSize;
	unsigned char *pucBuffer;


	/* Wait for a packet. */
	do
	{
		ulValue = mailbox_purr(NETX56_DPM_BOOT_NETX_RECEIVED_CMD);
	} while( ulValue==0 );

	/* Is the packet's size valid? */
	ulPacketSize = tDpm.ulHostToNetxDataSize;
	if( ulPacketSize==0 )
	{
		/* Send magic cookie and version info. */
		monitor_send_magic(NETX56_DPM_NETX_TO_HOST_BUFFERSIZE);
	}
	else if( ulPacketSize<=NETX56_DPM_HOST_TO_NETX_BUFFERSIZE )
	{
		/* Yes, the packet size is valid. */
		monitor_process_packet(tDpm.aucHostToNetxData, ulPacketSize, NETX56_DPM_NETX_TO_HOST_BUFFERSIZE);
	}
}



void transport_send_byte(unsigned char ucData)
{
	if( sizPacketOutputFill<NETX56_DPM_HOST_TO_NETX_BUFFERSIZE )
	{
		tDpm.aucNetxToHostData[sizPacketOutputFill] = ucData;
		++sizPacketOutputFill;
	}
/*
	else
	{
		uprintf("discarding byte 0x%02x\n", ucData);
	}
*/
}



void transport_send_packet(void)
{
	HOSTDEF(ptHandshakeDtcmArmMirrorArea);
	unsigned long ulValue;


	/* Send the packet. */
	tDpm.ulNetxToHostDataSize = sizPacketOutputFill;

	/* Toggle the 'packet send' flag. */
	ptHandshakeDtcmArmMirrorArea->aulHandshakeReg[0] ^= NETX56_DPM_BOOT_NETX_SEND_CMD<<SRT_NETX56_HANDSHAKE_REG_ARM_DATA;

	/* Wait for host ACK. */
	do
	{
		ulValue = mailbox_purr(NETX56_DPM_BOOT_NETX_SEND_CMD);
	} while( ulValue!=0 );

	/* Remember the packet size for resends. */
	sizPacketOutputFillLast = sizPacketOutputFill;

	sizPacketOutputFill = 0;
}



void transport_resend_packet(void)
{
	/* Restore the last packet size. */
	sizPacketOutputFill = sizPacketOutputFillLast;

	/* Send the buffer again. */
	transport_send_packet();
}



unsigned char transport_call_console_get(void)
{
	return 0;
}



void transport_call_console_put(unsigned int uiChar)
{
	/* Add the byte to the FIFO. */
	transport_send_byte((unsigned char)uiChar);

	/* Reached the maximum packet size? */
	if( sizPacketOutputFill>=NETX56_DPM_NETX_TO_HOST_BUFFERSIZE )
	{
		/* Yes -> send the packet. */
		transport_send_packet();

		/* Start a new packet. */
		transport_send_byte(MONITOR_STATUS_CallMessage);
	}

}



unsigned int transport_call_console_peek(void)
{
	return 0;
}



void transport_call_console_flush(void)
{
	/* Send the packet. */
	transport_send_packet();

	/* Start a new packet. */
	transport_send_byte(MONITOR_STATUS_CallMessage);
}
