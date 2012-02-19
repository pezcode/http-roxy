#ifndef SOCKET_H
#define SOCKET_H

#pragma once

#ifdef _WIN32
#include <winsock2.h> //IPv4
#include <ws2tcpip.h> //IPv6
#else
//#include <sys/types.h>
#include <sys/socket.h>
//#include <netinet/in.h>
//#include <arpa/inet.h>
#include <netdb.h> 
#endif
#include <string>
#include <cstdint>

using std::string;

class Address
{
friend class SocketAddress;
public:

	Address();
	Address(in_addr  addr4);
	Address(in6_addr addr6);

	static string getHostName();
	static Address fromHost(string host);
	static Address fromPresentation(string presentation);

	string toPresentation() const;

	bool isAny() const { return this->a.addr4.s_addr == INADDR_ANY; }

	operator in_addr()  const;
	operator in6_addr() const;

private:
	union
	{
		in_addr  addr4;
		in6_addr addr6;
	} a;
};

class SocketAddress
{
friend class Socket;
public:
	enum Family { INET = AF_INET, INET6 = AF_INET6 };
	typedef uint16_t port_t;

	SocketAddress(Family family = INET, Address address = Address(), port_t port = 0);
	SocketAddress(sockaddr_in  saddr);
	SocketAddress(sockaddr_in6 saddr6);

	Family getFamily() const;
	Address getAddress() const;
	port_t getPort() const;

	operator sockaddr_in() const;
	operator sockaddr_in6() const;

private:
	sockaddr_storage saddr;
};

class Socket
{
public:
	enum Domain { INET = PF_INET, INET6 = PF_INET6 };
	enum Type { STREAM = SOCK_STREAM, DGRAM = SOCK_DGRAM, SEQPACKET = SOCK_SEQPACKET, RAW = SOCK_RAW };
	enum Protocol { DEFAULT = 0 };

#ifdef _WIN32
	typedef SOCKET socket_t;
#else
	typedef int socket_t;
#endif

	Socket(socket_t socket) : socket(socket) { }
	Socket(bool IPV6 = false, bool TCP = true);
	Socket(Domain domain, Type type, Protocol protocol = DEFAULT);

	bool bind(SocketAddress addr);
	bool listen(unsigned int max = 5);
	Socket accept(SocketAddress* addr = NULL);
	bool connect(SocketAddress addr);

	int recv(char* buf, size_t max_size, bool force = false);
	size_t send(const char* buf, size_t size);

	bool shutdown(bool read = true, bool write = true);
	bool close();

	static bool startup();
	static bool unload();

	socket_t get() const { return this->socket; }
	bool valid() const { return this->socket != INVALID_SOCKET; }

	operator bool() const { return this->valid(); }
	operator socket_t() const { return this->get(); }

private:
	socket_t socket;
};

#endif
