
// net_null.c -- null network functions.

#include "common/q_common.h"

/*
===================
NET_CompareAdr
===================
*/
qboolean NET_CompareAdr(netadr_t a, netadr_t b)
{
    if (a.type != b.type)
        return false;

    if (a.type == NA_LOOPBACK)
        return true;

    if (a.type == NA_IP)
    {
        if (a.ip[0] == b.ip[0] && a.ip[1] == b.ip[1] && a.ip[2] == b.ip[2] && a.ip[3] == b.ip[3] && a.port == b.port)
            return true;
        return false;
    }

    if (a.type == NA_IPX)
    {
        if ((memcmp(a.ipx, b.ipx, 10) == 0) && a.port == b.port)
            return true;
        return false;
    }
}

/*
===================
NET_CompareBaseAdr

Compares without the port
===================
*/
qboolean NET_CompareBaseAdr(netadr_t a, netadr_t b)
{
    if (a.type != b.type)
        return false;

    if (a.type == NA_LOOPBACK)
        return true;

    if (a.type == NA_IP)
    {
        if (a.ip[0] == b.ip[0] && a.ip[1] == b.ip[1] && a.ip[2] == b.ip[2] && a.ip[3] == b.ip[3])
            return true;
        return false;
    }

    if (a.type == NA_IPX)
    {
        if ((memcmp(a.ipx, b.ipx, 10) == 0))
            return true;
        return false;
    }
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
qboolean NET_StringToAdr(char * s, netadr_t * a)
{
    return false;
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
=============================================================================

LOOPBACK BUFFERS FOR LOCAL PLAYER

=============================================================================
*/

qboolean NET_GetLoopPacket(netsrc_t sock, netadr_t * net_from, sizebuf_t * net_message)
{
    return false;
}

void NET_SendLoopPacket(netsrc_t sock, int length, void * data, netadr_t to)
{
}

//=============================================================================

qboolean NET_GetPacket(netsrc_t sock, netadr_t * net_from, sizebuf_t * net_message)
{
    return false;
}

//=============================================================================

void NET_SendPacket(netsrc_t sock, int length, void * data, netadr_t to)
{
}

//=============================================================================

/*
====================
NET_Socket
====================
*/
int NET_IPSocket(char * net_interface, int port)
{
    return 0;
}

/*
====================
NET_OpenIP
====================
*/
void NET_OpenIP(void)
{
}

/*
====================
IPX_Socket
====================
*/
int NET_IPXSocket(int port)
{
    return 0;
}

/*
====================
NET_OpenIPX
====================
*/
void NET_OpenIPX(void)
{
}

/*
====================
NET_Config

A single player game will only use the loopback code
====================
*/
void NET_Config(qboolean multiplayer)
{
}

// sleeps msec or until net socket is ready
void NET_Sleep(int msec)
{
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
