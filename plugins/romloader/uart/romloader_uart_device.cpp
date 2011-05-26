/***************************************************************************
 *   Copyright (C) 2008 by Christoph Thelen and M. Trensch                 *
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


#include "romloader_uart_device.h"

#include <stdio.h>
#include <string.h>

#ifdef _WINDOWS
#       define CRITICAL_SECTION_ENTER(cs) EnterCriticalSection(&cs)
#       define CRITICAL_SECTION_LEAVE(cs) LeaveCriticalSection(&cs)
#else
#       define CRITICAL_SECTION_ENTER(cs) pthread_mutex_lock(&cs)
#       define CRITICAL_SECTION_LEAVE(cs) pthread_mutex_unlock(&cs)

	static const pthread_mutex_t s_mutex_init = PTHREAD_MUTEX_INITIALIZER;
#endif


romloader_uart_device::romloader_uart_device(const char *pcPortName)
 : m_pcPortName(NULL)
 , m_ptFirstCard(NULL)
 , m_ptLastCard(NULL)
{
	m_pcPortName = strdup(pcPortName);

#ifdef _WINDOWS
	InitializeCriticalSection(&m_csCardLock);
#else
	memcpy(&m_csCardLock, &s_mutex_init, sizeof(pthread_mutex_t));
#endif
}


romloader_uart_device::~romloader_uart_device(void)
{
	deleteCards();

#ifdef _WINDOWS
	DeleteCriticalSection(&m_csCardLock);
#endif
}


void romloader_uart_device::initCards(void)
{
	tBufferCard *ptCard;


	if( m_ptFirstCard!=NULL )
	{
		deleteCards();
	}

	ptCard = new tBufferCard;
	ptCard->pucEnd = ptCard->aucData + mc_sizCardSize;
	ptCard->pucRead = ptCard->aucData;
	ptCard->pucWrite = ptCard->aucData;
	ptCard->ptNext = NULL;

	m_ptFirstCard = ptCard;
	m_ptLastCard = ptCard;
}


void romloader_uart_device::deleteCards(void)
{
	tBufferCard *ptCard;
	tBufferCard *ptNextCard;


	CRITICAL_SECTION_ENTER(m_csCardLock);

	ptCard = m_ptFirstCard;
	while( ptCard!=NULL )
	{
		ptNextCard = ptCard->ptNext;
		delete ptCard;
		ptCard = ptNextCard;
	}
	m_ptFirstCard = NULL;
	m_ptLastCard = NULL;

	CRITICAL_SECTION_LEAVE(m_csCardLock);
}


void romloader_uart_device::writeCards(const unsigned char *pucBuffer, size_t sizBufferSize)
{
	size_t sizLeft;
	size_t sizChunk;
	tBufferCard *ptCard;


	CRITICAL_SECTION_ENTER(m_csCardLock);

	sizLeft = sizBufferSize;
	while( sizLeft>0 )
	{
		// get free space in the current card
		sizChunk = m_ptLastCard->pucEnd - m_ptLastCard->pucWrite;
		// no more space -> create a new card
		if( sizChunk==0 )
		{
			fprintf(stderr, "New Card\n");

			ptCard = new tBufferCard;
			ptCard->pucEnd = ptCard->aucData + mc_sizCardSize;
			ptCard->pucRead = ptCard->aucData;
			ptCard->pucWrite = ptCard->aucData;
			ptCard->ptNext = NULL;
			// append new card
			m_ptLastCard->ptNext = ptCard;
			// close old card
			m_ptLastCard->pucWrite = NULL;
			// set the new last pointer
			m_ptLastCard = ptCard;
			// the new card is empty
			sizChunk = mc_sizCardSize;
		}
		// limit chunk to request size
		if( sizChunk>sizLeft )
		{
			sizChunk = sizLeft;
		}
		// copy data
		memcpy(m_ptLastCard->pucWrite, pucBuffer, sizChunk);
		// advance pointer
		m_ptLastCard->pucWrite += sizChunk;
		pucBuffer += sizChunk;
		sizLeft -= sizChunk;
	}

	CRITICAL_SECTION_LEAVE(m_csCardLock);

	/* Set the signal for received data. */
#ifndef _WINDOWS
	pthread_mutex_lock(&m_tRxDataAvail_Mutex);
	pthread_cond_signal(&m_tRxDataAvail_Condition);
	pthread_mutex_unlock(&m_tRxDataAvail_Mutex);
#endif
}


size_t romloader_uart_device::readCards(unsigned char *pucBuffer, size_t sizBuffer)
{
	size_t sizLeft;
	size_t sizRead;
	tBufferCard *ptOldCard;
	tBufferCard *ptNewCard;


	sizLeft = sizBuffer;
	do
	{
		if( m_ptFirstCard==NULL )
		{
			sizRead = 0;
			break;
		}
		else if( m_ptFirstCard->pucWrite!=NULL )
		{
			/* The first card is used by the write part -> lock the cards. */
			CRITICAL_SECTION_ENTER(m_csCardLock);

			/* Get the number of bytes left in this card. */
			sizRead = m_ptFirstCard->pucWrite - m_ptFirstCard->pucRead;
			if( sizRead>sizLeft )
			{
				sizRead = sizLeft;
			}
			/* Card can be empty. */
			if( sizRead>0 )
			{
				/* Copy the data. */
				memcpy(pucBuffer, m_ptFirstCard->pucRead, sizRead);
				/* Advance the read pointer. */
				m_ptFirstCard->pucRead += sizRead;
			}

			CRITICAL_SECTION_LEAVE(m_csCardLock);

			if( sizRead==0 )
			{
				/* No more data available. */
				break;
			}
		}
		else
		{
			// the first card is not used by the write part

			// get the number of bytes left in this card
			sizRead = m_ptFirstCard->pucEnd - m_ptFirstCard->pucRead;
			if( sizRead>sizLeft )
			{
				sizRead = sizLeft;
			}
			// card can be empty for overlapping buffer grow
			if( sizRead>0 )
			{
				// copy the data
				memcpy(pucBuffer, m_ptFirstCard->pucRead, sizRead);
				// advance the read pointer
				m_ptFirstCard->pucRead += sizRead;
			}
			else
			{
				fprintf(stderr, "WARNING: no remaining data: %d\n", sizRead);
			}
			// reached the end of the buffer?
			if( m_ptFirstCard->pucRead>=m_ptFirstCard->pucEnd )
			{
				// card is empty, move on to next card
				ptNewCard = m_ptFirstCard->ptNext;
				if( ptNewCard!=NULL )
				{
					// remember the empty card
					ptOldCard = m_ptFirstCard;
					// move to the new first card
					m_ptFirstCard = ptNewCard;
					// delete the empty card
					delete ptOldCard;
				}
			}
		}

		sizLeft -= sizRead;
		pucBuffer += sizRead;
	} while( sizLeft>0 );

	return sizBuffer-sizLeft;
}


size_t romloader_uart_device::getCardSize(void) const
{
	size_t sizData;
	tBufferCard *ptCard;


	sizData = 0;
	ptCard = m_ptFirstCard;
	while( ptCard!=NULL )
	{
		if( ptCard->pucWrite==NULL )
		{
			sizData += m_ptFirstCard->pucEnd - m_ptFirstCard->pucRead;
		}
		else
		{
			sizData += m_ptFirstCard->pucWrite - m_ptFirstCard->pucRead;
		}
		ptCard = ptCard->ptNext;
	}

	return sizData;
}


unsigned int romloader_uart_device::crc16(unsigned short usCrc, unsigned char ucData)
{
	usCrc  = (usCrc >> 8) | ((usCrc & 0xff) << 8);
	usCrc ^= ucData;
	usCrc ^= (usCrc & 0xff) >> 4;
	usCrc ^= (usCrc & 0x0f) << 12;
	usCrc ^= ((usCrc & 0xff) << 4) << 1;

	return usCrc;
}


bool romloader_uart_device::wait_for_prompt(unsigned long ulTimeout)
{
	bool fFoundPrompt;
	const size_t sizMaxSearch = 32;
	size_t sizCnt;
	size_t sizReceived;
	unsigned char ucData;


	fFoundPrompt = false;

	sizCnt = 0;
	do
	{
		sizReceived = RecvRaw(&ucData, 1, ulTimeout);
		printf("rec: 0x%02x, siz: %d\n", ucData, sizReceived);
		if( sizReceived!=1 )
		{
			/* Failed to receive the next char. */
			fprintf(stderr, "Failed to receive the knock response.\n");
			break;
		}
		else if( ucData=='>' )
		{
			fFoundPrompt = true;
			break;
		}
		else
		{
			++sizCnt;
		}
	} while( sizCnt<sizMaxSearch );

	return fFoundPrompt;
}


bool romloader_uart_device::GetLine(unsigned char **ppucLine, const char *pcEol, unsigned long ulTimeout)
{
	bool fOk;
	unsigned char *pucBuffer;
	unsigned char *pucBufferNew;
	size_t sizBufferCnt;
	size_t sizBufferMax;
	size_t sizEolSeq;
	size_t sizReceived;


	/* Receive char by char until the EOL sequence was received. */

	fprintf(stderr, "GetLine\n");

	/* Expect success. */
	fOk = true;

	sizEolSeq = strlen(pcEol);

	/* Init References array. */
	sizBufferCnt = 0;
	sizBufferMax = 80;
	pucBuffer = (unsigned char*)malloc(sizBufferMax);
	if( pucBuffer==NULL )
	{
		fprintf(stderr, "out of memory!\n");
		fOk = false;
	}
	else
	{
		do
		{
			sizReceived = RecvRaw(pucBuffer+sizBufferCnt, 1, ulTimeout);
			if( sizReceived!=1 )
			{
				/* Timeout, that's it... */
				fprintf(stderr, "Timeout!\n");

				fOk = false;
				break;
			}

			fprintf(stderr, "Recv: 0x%02x\n", pucBuffer[sizBufferCnt]);

			/* Check for EOL. */
			sizBufferCnt++;
			if( sizBufferCnt>=sizEolSeq && memcmp(pcEol, pucBuffer+sizBufferCnt-sizEolSeq, sizEolSeq)==0 )
			{
				break;
			}

			/* Is enough space in the array for one more entry? */
			/* NOTE: Do this at the end of the loop because the line is
			terminated with a '\0'. */
			if( sizBufferCnt>=sizBufferMax )
			{
				/* No -> expand the array. */
				sizBufferMax *= 2;
				/* Detect overflow or limitation. */
				if( sizBufferMax<=sizBufferCnt )
				{
					fOk = false;
					break;
				}
				/* Reallocate the array. */
				pucBufferNew = (unsigned char*)realloc(pucBuffer, sizBufferMax);
				if( pucBufferNew==NULL )
				{
					fOk = false;
					break;
				}
				pucBuffer = pucBufferNew;
			}
		} while( true );
	}

	if( fOk==true )
	{
		/* Terminate the line buffer. */
		pucBuffer[sizBufferCnt] = 0;
	}
	else if( pucBuffer!=NULL )
	{
		free(pucBuffer);
		pucBuffer = NULL;
	}

	*ppucLine = pucBuffer;

	return fOk;
}


const romloader_uart_device::ROMCODE_RESET_ID_T romloader_uart_device::atResIds[4] =
{
	{
		0xea080001,
		0x00200008,
		0x00001000,
		ROMLOADER_CHIPTYP_NETX500,
		"netX500",
		ROMLOADER_ROMCODE_ABOOT,
		"ABoot"
	},

	{
		0xea080002,
		0x00200008,
		0x00003002,
		ROMLOADER_CHIPTYP_NETX100,
		"netX100",
		ROMLOADER_ROMCODE_ABOOT,
		"ABoot"
	},

	{
		0xeac83ffc,
		0x08200008,
		0x00002001,
		ROMLOADER_CHIPTYP_NETX50,
		"netX50",
		ROMLOADER_ROMCODE_HBOOT,
		"HBoot"
	},

	{
		0xeafdfffa,
		0x08070008,
		0x00005003,
		ROMLOADER_CHIPTYP_NETX10,
		"netX10",
		ROMLOADER_ROMCODE_HBOOT,
		"HBoot"
	}
};


bool romloader_uart_device::legacy_read(unsigned long ulAddress, unsigned long *pulValue)
{
	union
	{
		unsigned char auc[32];
		char ac[32];
	} uCmd;
	union
	{
		unsigned char *puc;
		char *pc;
	} uResponse;
	size_t sizCmd;
	bool fOk;
	int iResult;
	unsigned long ulReadbackAddress;
	unsigned long ulValue;


	sizCmd = snprintf(uCmd.ac, 32, "DUMP %lx\n", ulAddress);
	/* Send knock sequence with 500ms second timeout. */
	if( SendRaw(uCmd.auc, sizCmd, 500)!=sizCmd )
	{
		/* Failed to send the command to the device. */
		fprintf(stderr, "Failed to send the command to the device.\n");
		fOk = false;
	}
	else
	{
		/* Receive one line. This is the command echo. */
		fOk = GetLine(&uResponse.puc, "\r\n", 2000);
		if( fOk==false )
		{
			fprintf(stderr, "failed to get command response!\n");
		}
		else
		{
			sizCmd = strlen(uResponse.pc);
			hexdump(uResponse.puc, sizCmd);
			free(uResponse.puc);

			/* Receive one line. This is the command result. */
			fOk = GetLine(&uResponse.puc, "\r\n>", 2000);
			if( fOk==false )
			{
				fprintf(stderr, "failed to get command response!\n");
			}
			else
			{
				iResult = sscanf(uResponse.pc, "%08lx: %08lx", &ulReadbackAddress, &ulValue);
				if( iResult==2 && ulAddress==ulReadbackAddress )
				{
					fprintf(stderr, "Yay, got result 0x%08x\n", ulValue);
					if( pulValue!=NULL )
					{
						*pulValue = ulValue;
					}
				}
				else
				{
					fprintf(stderr, "The command response is invalid!\n");
					fOk = false;
				}
				sizCmd = strlen(uResponse.pc);
				hexdump(uResponse.puc, sizCmd);
				free(uResponse.puc);
			}
		}
	}

	return fOk;
}


bool romloader_uart_device::update_device(void)
{
	bool fOk;
	unsigned long ulResetVector;
	unsigned long ulVersion;
	const ROMCODE_RESET_ID_T *ptCnt;
	const ROMCODE_RESET_ID_T *ptEnd;
	const ROMCODE_RESET_ID_T *ptDev;


	/* Read the reset vector. */
	fprintf(stderr, "Get reset vector\n");
	fOk = legacy_read(0U, &ulResetVector);
	if( fOk==true )
	{
		fprintf(stderr, "Reset vector: 0x%08x\n", ulResetVector);
		ptCnt = atResIds;
		ptEnd = atResIds + (sizeof(atResIds)/sizeof(atResIds[0]));
		ptDev = NULL;
		while( ptCnt<ptEnd )
		{
			fprintf(stderr, "Hit 0x%08x...\n", ptCnt->ulResetVector);
			if( ptCnt->ulResetVector==ulResetVector )
			{
				fOk = legacy_read(ptCnt->ulVersionAddress, &ulVersion);
				if( fOk==true )
				{
					if( ptCnt->ulVersionValue==ulVersion )
					{
						ptDev = ptCnt;
						break;
					}
				}
				else
				{
					fprintf(stderr, "Failed to read the version data at address 0x%08x!\n", ptCnt->ulVersionAddress);
					break;
				}
			}
			++ptCnt;
		}

		if( ptDev==NULL )
		{
			fprintf(stderr, "Failed to identify the device!\n");
			fOk = false;
		}
		else
		{
			fprintf(stderr, "Found %s on %s.\n", ptDev->pcRomcodeName, ptDev->pcChiptypName);
		}
			
	}
	else
	{
		fprintf(stderr, "Failed to read the reset vector!\n");
	}
	
	return fOk;
}


bool romloader_uart_device::IdentifyLoader(void)
{
	bool fResult = false;
	const unsigned char aucKnock[5] = { '*', 0x00, 0x00, '*', '#' };
	const unsigned char aucKnockResponseMi[7] = { 0x09, 0x00, 0x00, 0x4d, 0x4f, 0x4f, 0x48 };
	unsigned char aucData[13];
	unsigned char *pucResponse;
	size_t sizCnt;
	size_t sizTransfered;
	unsigned long ulMiVersionMaj;
	unsigned long ulMiVersionMin;
	bool fDeviceNeedsUpdate;
	unsigned short usCrc;
	const unsigned char aucBlankLine[1] = { '\n' };


	/* No update by default. */
	fDeviceNeedsUpdate = false;

	/* Send knock sequence with 500ms second timeout. */
	if( SendRaw(aucKnock, sizeof(aucKnock), 500)!=sizeof(aucKnock) )
	{
		/* Failed to send knock sequence to device. */
		fprintf(stderr, "Failed to send knock sequence to device.\n");
	}
	else if( Flush()!=true )
	{
		/* Failed to flush the knock sequence. */
		fprintf(stderr, "Failed to flush the knock sequence.\n");
	}
	else
	{
		sizTransfered = RecvRaw(aucData, 1, 1000);
		if( sizTransfered!=1 )
		{
			/* Failed to receive first char of knock response. */
			fprintf(stderr, "Failed to receive first char of knock response.\n");
		}
		else
		{
			printf("received knock response: 0x%02x\n", aucData[0]);
			/* Knock echoed -> this is the prompt or the machine interface. */
			if( aucData[0]=='*' )
			{
				printf("ok, received star!\n");

				/* Get rest of the response. */
				sizTransfered = RecvRaw(aucData, 13, 500);
				hexdump(aucData, sizTransfered);
				printf("%d\n", sizTransfered);
				hexdump(aucKnockResponseMi, 7);
				if( sizTransfered==0 )
				{
					fprintf(stderr, "Failed to receive response after the star!\n");
				}
				else if( sizTransfered==13 && memcmp(aucData, aucKnockResponseMi, 7)==0 )
				{
					/* Build the crc for the packet. */
					usCrc = 0;
					for(sizCnt==0; sizCnt<13; ++sizCnt)
					{
						usCrc = crc16(usCrc, aucData[sizCnt]);
					}
					if( usCrc!=0 )
					{
						fprintf(stderr, "Crc of version packet is invalid!\n");
					}
					else
					{
						ulMiVersionMin = aucData[7] | (aucData[8]<<8);;
						ulMiVersionMaj = aucData[9] | (aucData[10]<<8);
						printf("Found new machine interface V%ld.%ld.\n", ulMiVersionMaj, ulMiVersionMin);
						fResult = true;
					}
				}
				else if( aucData[0]!='#' )
				{
					printf("received strange response after the star: 0x%02x\n", aucData[0]);
				}
				else
				{
					printf("ok, received hash!\n");
					fDeviceNeedsUpdate = true;
					fResult = true;
				}
			}
			else if( aucData[0]=='#' )
			{
				printf("ok, received hash!\n");

				fDeviceNeedsUpdate = true;
				fResult = true;
			}
			else
			{
				/* This seems to be the welcome message. */

				/* The welcome message can be quite trashed depending on the driver. Just discard the characters until the first timeout and send enter. */

				/* Discard all data until timeout. */
				do
				{
					sizTransfered = RecvRaw(aucData, 1, 200);
					if( sizTransfered==1 )
					{
						fprintf(stderr, "Discarding 0x%02x\n", aucData[0]);
					}
				} while( sizTransfered==1 );

				/* Send enter. */
				sizTransfered = SendRaw(aucBlankLine, sizeof(aucBlankLine), 200);
				if( sizTransfered!=1 )
				{
					fprintf(stderr, "Failed to send enter to device!\n");
				}
				else
				{
					/* Receive the rest of the line until '>'. Discard the data. */
					printf("receive the rest of the knock response\n");
					fResult = wait_for_prompt(200);
					if( fResult==true )
					{
						/* Found the prompt. */
						fDeviceNeedsUpdate = true;
					}
					else
					{
						fprintf(stderr, "received strange response after romloader message!\n");
					}
				}
			}
		}
	}

	if( fResult==true )
	{
		if( fDeviceNeedsUpdate==true )
		{
//				fResult = WaitForResponse(&pucResponse, 65536, 1024);
/* TODO:
 1) grab data until next newline + '>' Combo.
 2) download new code
 3) start
*/
			fResult = update_device();

		}
	}


	return fResult;
}


void romloader_uart_device::hexdump(const unsigned char *pucData, unsigned long ulSize)
{
	const unsigned char *pucDumpCnt, *pucDumpEnd;
	unsigned long ulAddressCnt;
	size_t sizBytesLeft;
	size_t sizChunkSize;
	size_t sizChunkCnt;


	// show a hexdump of the data
	pucDumpCnt = pucData;
	pucDumpEnd = pucData + ulSize;
	ulAddressCnt = 0;
	while( pucDumpCnt<pucDumpEnd )
	{
		// get number of bytes for the next line
		sizChunkSize = 16;
		sizBytesLeft = pucDumpEnd - pucDumpCnt;
		if( sizChunkSize>sizBytesLeft )
		{
			sizChunkSize = sizBytesLeft;
		}

		// start a line in the dump with the address
		printf("%08lX: ", ulAddressCnt);
		// append the data bytes
		sizChunkCnt = sizChunkSize;
		while( sizChunkCnt!=0 )
		{
			printf("%02X ", *(pucDumpCnt++));
			--sizChunkCnt;
		}
		// next line
		printf("\n");
		ulAddressCnt += sizChunkSize;
	}
}
