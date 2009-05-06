/***************************************************************************
 *   Copyright (C) 2009 by Christoph Thelen                                *
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


#include <stdio.h>
#include <string.h>
#include <vector>

#include "muhkuh_plugin_interface.h"


/*-----------------------------------*/

#define SIZ_MAX_MUHKUH_PLUGIN_STRING 256


muhkuh_plugin::muhkuh_plugin(const char *pcName, const char *pcTyp, muhkuh_plugin_provider *ptProvider)
 : m_pcName(NULL)
 , m_pcTyp(NULL)
 , m_ptProvider(ptProvider)
 , m_fIsConnected(false)
{
	// clone name and typ
	m_pcName = clone_string(pcName, SIZ_MAX_MUHKUH_PLUGIN_STRING);
	m_pcTyp = clone_string(pcTyp, SIZ_MAX_MUHKUH_PLUGIN_STRING);

	printf("%s(%p): created\n", m_pcName, this);
}


muhkuh_plugin::~muhkuh_plugin(void)
{
	bool fOk;


	if( m_ptProvider!=NULL )
	{
		fOk = m_ptProvider->ReleaseInterface(this);
		if( fOk!=true )
		{
			printf("%s(%p): failed to release interface!\n", m_pcName, this);
		}
	}

	printf("%s(%p): deleted\n", m_pcName, this);

	if( m_pcName!=NULL )
	{
		delete[] m_pcName;
	}
	if( m_pcTyp!=NULL )
	{
		delete[] m_pcTyp;
	}
}


// returns the connection state of the device
bool muhkuh_plugin::IsConnected(void) const
{
	printf("%s(%p): is_connected() = %s\n", m_pcName, this, (m_fIsConnected?"true":"false"));

	return m_fIsConnected;
}


// returns the device name
const char *muhkuh_plugin::GetName(void) const
{
	return m_pcName;
}


// returns the device typ
const char *muhkuh_plugin::GetTyp(void) const
{
	return m_pcTyp;
}


char *muhkuh_plugin::clone_string(const char *pcStr, size_t sizMax)
{
	char *pcClone;
	size_t sizStr;


	pcClone = NULL;
	if( pcStr!=NULL )
	{
		sizStr = strlen(pcStr);
		if( sizStr>sizMax-1 )
		{
			sizStr = sizMax-1;
		}
		pcClone = new char[sizStr+1];
		memcpy(pcClone, pcStr, sizStr);
		pcClone[sizStr] = 0;
	}

	return pcClone;
}


/*-----------------------------------*/


muhkuh_plugin_reference::muhkuh_plugin_reference(void)
 : m_fIsValid(false)
 , m_pcName(NULL)
 , m_pcTyp(NULL)
 , m_fIsUsed(false)
{
}


muhkuh_plugin_reference::muhkuh_plugin_reference(const char *pcName, const char *pcTyp, bool fIsUsed)
 : m_fIsValid(true)
 , m_fIsUsed(m_fIsUsed)
{
	m_pcName = clone_string(pcName, SIZ_MAX_MUHKUH_PLUGIN_STRING);
	m_pcTyp = clone_string(pcTyp, SIZ_MAX_MUHKUH_PLUGIN_STRING);
}


muhkuh_plugin_reference::muhkuh_plugin_reference(const muhkuh_plugin_reference *ptCloneMe)
 : m_fIsValid(true)
 , m_fIsUsed(ptCloneMe->m_fIsUsed)
{
	m_pcName = clone_string(ptCloneMe->m_pcName, SIZ_MAX_MUHKUH_PLUGIN_STRING);
	m_pcTyp = clone_string(ptCloneMe->m_pcTyp, SIZ_MAX_MUHKUH_PLUGIN_STRING);
}


bool muhkuh_plugin_reference::IsValid(void) const
{
	return m_fIsValid;
}


const char *muhkuh_plugin_reference::GetName(void) const
{
	return m_pcName;
}


const char *muhkuh_plugin_reference::GetTyp(void) const
{
	return m_pcTyp;
}


bool muhkuh_plugin_reference::IsUsed(void) const
{
	return m_fIsUsed;
}


char *muhkuh_plugin_reference::clone_string(const char *pcStr, size_t sizMax)
{
	char *pcClone;
	size_t sizStr;


	pcClone = NULL;
	if( pcStr!=NULL )
	{
		sizStr = strlen(pcStr);
		if( sizStr>sizMax-1 )
		{
			sizStr = sizMax-1;
		}
		pcClone = new char[sizStr+1];
		memcpy(pcClone, pcStr, sizStr);
		pcClone[sizStr] = 0;
	}

	return pcClone;
}


/*-----------------------------------*/


muhkuh_plugin_provider::muhkuh_plugin_provider(const char *pcPluginId)
{
	// init the plugin description with default values
	m_pt_plugin_desc.pcPluginName = NULL;
	m_pt_plugin_desc.pcPluginId = NULL;
	m_pt_plugin_desc.tVersion.uiVersionMajor = 0;
	m_pt_plugin_desc.tVersion.uiVersionMinor = 0;
	m_pt_plugin_desc.tVersion.uiVersionSub = 0;

	// copy the id
	m_pt_plugin_desc.pcPluginId = clone_string(pcPluginId, SIZ_MAX_MUHKUH_PLUGIN_STRING);
}


muhkuh_plugin_provider::~muhkuh_plugin_provider(void)
{
	if( m_pt_plugin_desc.pcPluginName!=NULL )
	{
		delete[] m_pt_plugin_desc.pcPluginName;
	}
	if( m_pt_plugin_desc.pcPluginId!=NULL )
	{
		delete[] m_pt_plugin_desc.pcPluginId;
	}
}


const muhkuh_plugin_desc *muhkuh_plugin_provider::GetDesc(void) const
{
	return &m_pt_plugin_desc;
}


char *muhkuh_plugin_provider::clone_string(const char *pcStr, size_t sizMax)
{
	char *pcClone;
	size_t sizStr;


	pcClone = NULL;
	if( pcStr!=NULL )
	{
		sizStr = strlen(pcStr);
		if( sizStr>sizMax-1 )
		{
			sizStr = sizMax-1;
		}
		pcClone = new char[sizStr+1];
		memcpy(pcClone, pcStr, sizStr);
		pcClone[sizStr] = 0;
	}

	return pcClone;
}

