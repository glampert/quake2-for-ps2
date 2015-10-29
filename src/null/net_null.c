/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

// net_null.c -- null network driver.

#include "common/q_common.h"

/*
===================
NET_CompareAdr
===================
*/
qboolean NET_CompareAdr(netadr_t a, netadr_t b)
{
    if (a.type != b.type)
    {
        return false;
    }

    if (a.type == NA_LOOPBACK)
    {
        return true;
    }

    if (a.type == NA_IP)
    {
        if (a.ip[0] == b.ip[0] && a.ip[1] == b.ip[1] && a.ip[2] == b.ip[2] && a.ip[3] == b.ip[3] && a.port == b.port)
        {
            return true;
        }
        return false;
    }

    if (a.type == NA_IPX)
    {
        if ((memcmp(a.ipx, b.ipx, 10) == 0) && a.port == b.port)
        {
            return true;
        }
        return false;
    }
}

/*
===================
NET_CompareBaseAdr
===================
*/
qboolean NET_CompareBaseAdr(netadr_t a, netadr_t b)
{
    // Compares without the port

    if (a.type != b.type)
    {
        return false;
    }

    if (a.type == NA_LOOPBACK)
    {
        return true;
    }

    if (a.type == NA_IP)
    {
        if (a.ip[0] == b.ip[0] && a.ip[1] == b.ip[1] && a.ip[2] == b.ip[2] && a.ip[3] == b.ip[3])
        {
            return true;
        }
        return false;
    }

    if (a.type == NA_IPX)
    {
        if ((memcmp(a.ipx, b.ipx, 10) == 0))
        {
            return true;
        }
        return false;
    }
}

/*
=============
NET_IsLocalAddress
=============
*/
qboolean NET_IsLocalAddress(netadr_t adr)
{
    return adr.type == NA_LOOPBACK;
}

/*
===================
NET_AdrToString
===================
*/
char * NET_AdrToString(netadr_t a)
{
    static char s[64];
    return s;
}

/*
=============
NET_StringToAdr
=============
*/
qboolean NET_StringToAdr(const char * s, netadr_t * a)
{
    return false;
}

/*
====================
NET_GetPacket
====================
*/
qboolean NET_GetPacket(netsrc_t sock, netadr_t * net_from, sizebuf_t * net_message)
{
    return false;
}

/*
====================
NET_SendPacket
====================
*/
void NET_SendPacket(netsrc_t sock, int length, const void * data, netadr_t to)
{
}

/*
====================
NET_Config
====================
*/
void NET_Config(qboolean multiplayer)
{
    // A single player game will only use the loopback code
}

/*
====================
NET_Sleep
====================
*/
void NET_Sleep(int msec)
{
    // Sleeps msec or until net socket is ready
}

/*
====================
NET_Init
====================
*/
void NET_Init(void)
{
}

/*
====================
NET_Shutdown
====================
*/
void NET_Shutdown(void)
{
}
