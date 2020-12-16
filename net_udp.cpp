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
// net_udp.c

#include "quakedef.hpp"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <cerrno>
#include <unistd.h>

#ifdef __sun__
#include <sys/filio.h>
#endif

#ifdef NeXT
#include <libc.h>
#endif
// fixme: what library was this linking with?
//extern int gethostname (char * chr, int i);

// fixme: what library was this linking with?
//int close (int);

extern cvar_t hostname;

static int net_acceptsocket = -1;		// socket for fielding new connections
static int net_controlsocket;
static int net_broadcastsocket = 0;
static struct qsockaddr broadcastaddr;

static unsigned long myAddr;

#include "net_udp.hpp"

//=============================================================================

auto UDP_Init () -> int
{
	struct hostent *local = nullptr;
	char	buff[MAXHOSTNAMELEN];
	struct qsockaddr addr{};
	char *colon = nullptr;
	
	if (COM_CheckParm ("-noudp"))
		return -1;

	// determine my name & address
	gethostname(buff, MAXHOSTNAMELEN);
	local = gethostbyname(buff);
	myAddr = *(int *)local->h_addr_list[0];

	// if the quake hostname isn't set, set it to the machine name
	if (Q_strcmp(hostname.string, "UNNAMED") == 0)
	{
		buff[15] = 0;
		Cvar_Set ("hostname", buff);
	}

	if ((net_controlsocket = UDP_OpenSocket (0)) == -1)
		Sys_Error("UDP_Init: Unable to open control socket\n");

	((struct sockaddr_in *)&broadcastaddr)->sin_family = AF_INET;
	((struct sockaddr_in *)&broadcastaddr)->sin_addr.s_addr = INADDR_BROADCAST;
	((struct sockaddr_in *)&broadcastaddr)->sin_port = htons(net_hostport);

	UDP_GetSocketAddr (net_controlsocket, &addr);
	Q_strcpy(my_tcpip_address,  UDP_AddrToString (&addr));
	colon = Q_strrchr (my_tcpip_address, ':');
	if (colon)
		*colon = 0;

	Con_Printf("UDP Initialized\n");
	tcpipAvailable = true;

	return net_controlsocket;
}

//=============================================================================

void UDP_Shutdown ()
{
	UDP_Listen (false);
	UDP_CloseSocket (net_controlsocket);
}

//=============================================================================

void UDP_Listen (qboolean state)
{
	// enable listening
	if (state)
	{
		if (net_acceptsocket != -1)
			return;
		if ((net_acceptsocket = UDP_OpenSocket (net_hostport)) == -1)
			Sys_Error ("UDP_Listen: Unable to open accept socket\n");
		return;
	}

	// disable listening
	if (net_acceptsocket == -1)
		return;
	UDP_CloseSocket (net_acceptsocket);
	net_acceptsocket = -1;
}

//=============================================================================

auto UDP_OpenSocket (int port) -> int
{
	int newsocket = 0;
	sockaddr_in address{};
	qboolean _true = true;

	if ((newsocket = socket (PF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
		return -1;

#if 0
	if (ioctl (newsocket, FIONBIO, (char *)&_true) == -1)
		goto ErrorReturn;
#endif
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(port);

	// FIXME: weird pointer casting magic
	if(bind (newsocket, (const struct sockaddr *) &address, sizeof(address)) == -1)
		goto ErrorReturn;

	return newsocket;

ErrorReturn:
	close (newsocket);
	return -1;
}

//=============================================================================

auto UDP_CloseSocket (int socket) -> int
{
	if (socket == net_broadcastsocket)
		net_broadcastsocket = 0;
	return close (socket);
}


//=============================================================================
/*
============
PartialIPAddress

this lets you type only as much of the net address as required, using
the local network components to fill in the rest
============
*/
static auto PartialIPAddress (char *in, struct qsockaddr *hostaddr) -> int
{
	char buff[256];
	char *b = nullptr;
	int addr = 0;
	int num = 0;
	int mask = 0;
	int run = 0;
	int port = 0;
	
	buff[0] = '.';
	b = buff;
	strcpy(buff+1, in);
	if (buff[1] == '.')
		b++;

	addr = 0;
	mask=-1;
	while (*b == '.')
	{
		b++;
		num = 0;
		run = 0;
		while (!( *b < '0' || *b > '9'))
		{
		  num = num*10 + *b++ - '0';
		  if (++run > 3)
		  	return -1;
		}
		if ((*b < '0' || *b > '9') && *b != '.' && *b != ':' && *b != 0)
			return -1;
		if (num < 0 || num > 255)
			return -1;
		mask<<=8;
		addr = (addr<<8) + num;
	}
	
	if (*b++ == ':')
		port = Q_atoi(b);
	else
		port = net_hostport;

	hostaddr->sa_family = AF_INET;
	((struct sockaddr_in *)hostaddr)->sin_port = htons((short)port);	
	((struct sockaddr_in *)hostaddr)->sin_addr.s_addr = (myAddr & htonl(mask)) | htonl(addr);
	
	return 0;
}
//=============================================================================

auto UDP_Connect (int socket, struct qsockaddr *addr) -> int
{
	return 0;
}

//=============================================================================

auto UDP_CheckNewConnections () -> int
{
	unsigned long	available = 0;

	if (net_acceptsocket == -1)
		return -1;

	if (ioctl (net_acceptsocket, FIONREAD, &available) == -1)
		Sys_Error ("UDP: ioctlsocket (FIONREAD) failed\n");
	if (available)
		return net_acceptsocket;
	return -1;
}

//=============================================================================

auto UDP_Read (int socket, byte *buf, int len, struct qsockaddr *addr) -> int
{
	int addrlen = sizeof (struct qsockaddr);
	int ret = 0;

	ret = recvfrom (socket, buf, len, 0, (struct sockaddr *)addr, reinterpret_cast<socklen_t*>(&addrlen));
	if (ret == -1 && (errno == EWOULDBLOCK || errno == ECONNREFUSED))
		return 0;
	return ret;
}

//=============================================================================

auto UDP_MakeSocketBroadcastCapable (int socket) -> int
{
	int				i = 1;

	// make this socket broadcast capable
	if (setsockopt(socket, SOL_SOCKET, SO_BROADCAST, (char *)&i, sizeof(i)) < 0)
		return -1;
	net_broadcastsocket = socket;

	return 0;
}

//=============================================================================

auto UDP_Broadcast (int socket, byte *buf, int len) -> int
{
	int ret = 0;

	if (socket != net_broadcastsocket)
	{
		if (net_broadcastsocket != 0)
			Sys_Error("Attempted to use multiple broadcasts sockets\n");
		ret = UDP_MakeSocketBroadcastCapable (socket);
		if (ret == -1)
		{
			Con_Printf("Unable to make socket broadcast capable\n");
			return ret;
		}
	}

	return UDP_Write (socket, buf, len, &broadcastaddr);
}

//=============================================================================

auto UDP_Write (int socket, byte *buf, int len, struct qsockaddr *addr) -> int
{
	int ret = 0;

	ret = sendto (socket, buf, len, 0, (struct sockaddr *)addr, sizeof(struct qsockaddr));
	if (ret == -1 && errno == EWOULDBLOCK)
		return 0;
	return ret;
}

//=============================================================================

auto UDP_AddrToString (struct qsockaddr *addr) -> char *
{
	static char buffer[22];
	int haddr = 0;

	haddr = ntohl(((struct sockaddr_in *)addr)->sin_addr.s_addr);
	sprintf(buffer, "%d.%d.%d.%d:%d", (haddr >> 24) & 0xff, (haddr >> 16) & 0xff, (haddr >> 8) & 0xff, haddr & 0xff, ntohs(((struct sockaddr_in *)addr)->sin_port));
	return buffer;
}

//=============================================================================

auto UDP_StringToAddr (char *string, struct qsockaddr *addr) -> int
{
	int ha1 = 0, ha2 = 0, ha3 = 0, ha4 = 0, hp = 0;
	int ipaddr = 0;

	sscanf(string, "%d.%d.%d.%d:%d", &ha1, &ha2, &ha3, &ha4, &hp);
	ipaddr = (ha1 << 24) | (ha2 << 16) | (ha3 << 8) | ha4;

	addr->sa_family = AF_INET;
	((struct sockaddr_in *)addr)->sin_addr.s_addr = htonl(ipaddr);
	((struct sockaddr_in *)addr)->sin_port = htons(hp);
	return 0;
}

//=============================================================================

auto UDP_GetSocketAddr (int socket, struct qsockaddr *addr) -> int
{
	auto inet_addr = [](const char* cp) {
		int ha1 = 0, ha2 = 0, ha3 = 0, ha4 = 0;

		int ret = sscanf(cp, "%d.%d.%d.%d", &ha1, &ha2, &ha3, &ha4);
		if (ret != 4)
			return -1;
		
		return (ha1 << 24) | (ha2 << 16) | (ha3 << 8) | ha4;
	};

	int addrlen = sizeof(struct qsockaddr);
	unsigned int a = 0;

	Q_memset(addr, 0, sizeof(struct qsockaddr));
	getsockname(socket, (struct sockaddr *)addr, reinterpret_cast<socklen_t*>(&addrlen));
	a = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
	if (a == 0 || a == inet_addr("127.0.0.1"))
		((struct sockaddr_in *)addr)->sin_addr.s_addr = myAddr;

	return 0;
}

//=============================================================================

auto UDP_GetNameFromAddr (struct qsockaddr *addr, char *name) -> int
{
	struct hostent *hostentry = nullptr;

	hostentry = gethostbyaddr ((char *)&((struct sockaddr_in *)addr)->sin_addr, sizeof(struct in_addr), AF_INET);
	if (hostentry)
	{
		Q_strncpy (name, (char *)hostentry->h_name, NET_NAMELEN - 1);
		return 0;
	}

	Q_strcpy (name, UDP_AddrToString (addr));
	return 0;
}

//=============================================================================

auto UDP_GetAddrFromName(char *name, struct qsockaddr *addr) -> int
{
	struct hostent *hostentry = nullptr;

	if (name[0] >= '0' && name[0] <= '9')
		return PartialIPAddress (name, addr);
	
	hostentry = gethostbyname (name);
	if (!hostentry)
		return -1;

	addr->sa_family = AF_INET;
	((struct sockaddr_in *)addr)->sin_port = htons(net_hostport);	
	((struct sockaddr_in *)addr)->sin_addr.s_addr = *(int *)hostentry->h_addr_list[0];

	return 0;
}

//=============================================================================

auto UDP_AddrCompare (struct qsockaddr *addr1, struct qsockaddr *addr2) -> int
{
	if (addr1->sa_family != addr2->sa_family)
		return -1;

	if (((struct sockaddr_in *)addr1)->sin_addr.s_addr != ((struct sockaddr_in *)addr2)->sin_addr.s_addr)
		return -1;

	if (((struct sockaddr_in *)addr1)->sin_port != ((struct sockaddr_in *)addr2)->sin_port)
		return 1;

	return 0;
}

//=============================================================================

auto UDP_GetSocketPort (struct qsockaddr *addr) -> int
{
	return ntohs(((struct sockaddr_in *)addr)->sin_port);
}


auto UDP_SetSocketPort (struct qsockaddr *addr, int port) -> int
{
	((struct sockaddr_in *)addr)->sin_port = htons(port);
	return 0;
}

//=============================================================================
