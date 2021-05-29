/*
Copyright (C) 1996-1997 Id Software, Inc.

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
// net_loop.c

#include "quakedef.hpp"
#include "net_loop.hpp"

qboolean localconnectpending = false;
qsocket_t *loop_client = nullptr;
qsocket_t *loop_server = nullptr;

auto Loop_Init() -> int {
    if (cls.state == ca_dedicated)
        return -1;
    return 0;
}


void Loop_Shutdown() {
}


void Loop_Listen(qboolean state) {
}


void Loop_SearchForHosts(qboolean xmit) {
    if (!sv.active)
        return;

    hostCacheCount = 1;
    if (Q_strcmp(hostname.string, "UNNAMED") == 0)
        hostcache[0].name = "local";
    else
        hostcache[0].name = hostname.string;
    hostcache[0].map = sv.name;
    hostcache[0].users = net_activeconnections;
    hostcache[0].maxusers = svs.maxclients;
    hostcache[0].driver = net_driverlevel;
    hostcache[0].cname = "local";
}


auto Loop_Connect(char *host) -> qsocket_t * {
    if (Q_strcmp(host, "local") != 0)
        return nullptr;

    localconnectpending = true;

    if (!loop_client) {
        if ((loop_client = NET_NewQSocket()) == nullptr) {
            Con_Printf("Loop_Connect: no qsocket available\n");
            return nullptr;
        }
        Q_strcpy(loop_client->address, "localhost");
    }
    loop_client->receiveMessageLength = 0;
    loop_client->sendMessageLength = 0;
    loop_client->canSend = true;

    if (!loop_server) {
        if ((loop_server = NET_NewQSocket()) == nullptr) {
            Con_Printf("Loop_Connect: no qsocket available\n");
            return nullptr;
        }
        Q_strcpy(loop_server->address, "LOCAL");
    }
    loop_server->receiveMessageLength = 0;
    loop_server->sendMessageLength = 0;
    loop_server->canSend = true;

    loop_client->driverdata = (void *) loop_server;
    loop_server->driverdata = (void *) loop_client;

    return loop_client;
}


auto Loop_CheckNewConnections() -> qsocket_t * {
    if (!localconnectpending)
        return nullptr;

    localconnectpending = false;
    loop_server->sendMessageLength = 0;
    loop_server->receiveMessageLength = 0;
    loop_server->canSend = true;
    loop_client->sendMessageLength = 0;
    loop_client->receiveMessageLength = 0;
    loop_client->canSend = true;
    return loop_server;
}


static auto IntAlign(int value) -> int {
    return (value + (sizeof(int) - 1)) & (~(sizeof(int) - 1));
}


auto Loop_GetMessage(qsocket_t *sock) -> int {
    int ret = 0;
    int length = 0;

    if (sock->receiveMessageLength == 0)
        return 0;

    ret = sock->receiveMessage[0];
    length = sock->receiveMessage[1] + (sock->receiveMessage[2] << 8);
    // alignment byte skipped here
    SZ_Clear(&net_message);
    SZ_Write(&net_message, &sock->receiveMessage[4], length);

    length = IntAlign(length + 4);
    sock->receiveMessageLength -= length;

    if (sock->receiveMessageLength)
        Q_memcpy(sock->receiveMessage, &sock->receiveMessage[length], sock->receiveMessageLength);

    if (sock->driverdata && ret == 1)
        ((qsocket_t *) sock->driverdata)->canSend = true;

    return ret;
}


auto Loop_SendMessage(qsocket_t *sock, sizebuf_t *data) -> int {
    byte *buffer = nullptr;
    int *bufferLength = nullptr;

    if (!sock->driverdata)
        return -1;

    bufferLength = &((qsocket_t *) sock->driverdata)->receiveMessageLength;

    if ((*bufferLength + data->cursize + 4) > NET_MAXMESSAGE)
        Sys_Error("Loop_SendMessage: overflow\n");

    buffer = ((qsocket_t *) sock->driverdata)->receiveMessage + *bufferLength;

    // message type
    *buffer++ = 1;

    // length
    *buffer++ = data->cursize & 0xff;
    *buffer++ = data->cursize >> 8;

    // align
    buffer++;

    // message
    Q_memcpy(buffer, data->data, data->cursize);
    *bufferLength = IntAlign(*bufferLength + data->cursize + 4);

    sock->canSend = false;
    return 1;
}


auto Loop_SendUnreliableMessage(qsocket_t *sock, sizebuf_t *data) -> int {
    byte *buffer = nullptr;
    int *bufferLength = nullptr;

    if (!sock->driverdata)
        return -1;

    bufferLength = &((qsocket_t *) sock->driverdata)->receiveMessageLength;

    if ((*bufferLength + data->cursize + sizeof(byte) + sizeof(short)) > NET_MAXMESSAGE)
        return 0;

    buffer = ((qsocket_t *) sock->driverdata)->receiveMessage + *bufferLength;

    // message type
    *buffer++ = 2;

    // length
    *buffer++ = data->cursize & 0xff;
    *buffer++ = data->cursize >> 8;

    // align
    buffer++;

    // message
    Q_memcpy(buffer, data->data, data->cursize);
    *bufferLength = IntAlign(*bufferLength + data->cursize + 4);
    return 1;
}


auto Loop_CanSendMessage(qsocket_t *sock) -> qboolean {
    if (!sock->driverdata)
        return false;
    return sock->canSend;
}


auto Loop_CanSendUnreliableMessage(qsocket_t *sock) -> qboolean {
    return true;
}


void Loop_Close(qsocket_t *sock) {
    if (sock->driverdata)
        ((qsocket_t *) sock->driverdata)->driverdata = nullptr;
    sock->receiveMessageLength = 0;
    sock->sendMessageLength = 0;
    sock->canSend = true;
    if (sock == loop_client)
        loop_client = nullptr;
    else
        loop_server = nullptr;
}
