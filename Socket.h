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

class Address
{
friend class SocketAddress;
public:

	Address();
	Address(in_addr  addr4);
	Address(in6_addr addr6);

	static std::string getHostName();
	static Address fromHost(const std::string& host);
	static Address fromPresentation(const std::string& presentation);

	std::string toPresentation() const;

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
	enum Type { STREAM = SOCK_STREAM, DGRAM = SOCK_DGRAM, RAW = SOCK_RAW, RDM = SOCK_RDM, SEQPACKET = SOCK_SEQPACKET };
	enum Protocol { DEFAULT = 0 };

	enum RecvFlag { NONE = 0, PEEK = MSG_PEEK, OOB = MSG_OOB };

#ifdef _WIN32
	typedef SOCKET socket_t;
#else
	typedef int socket_t;
#endif

	Socket(socket_t socket = INVALID_SOCKET) : socket(socket) { }
	Socket(Domain domain, Type type, Protocol protocol = DEFAULT);

	bool bind(SocketAddress addr);
	bool listen(unsigned int max = 5);
	Socket accept(SocketAddress* addr = NULL);
	bool connect(SocketAddress addr);

	int recv(char* buf, size_t max_size, RecvFlag flags = NONE, bool force = false);
	size_t send(const char* buf, size_t size);

	// seconds < 0 -> infinite
	bool select_read(long seconds, long microseconds = 0) const;
	bool select_write(long seconds, long microseconds = 0) const;

	bool shutdown(int how);
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

	void prepare_select(long seconds, long microseconds, fd_set* fd_desc, timeval** timeval_ptr) const;
};

#endif
