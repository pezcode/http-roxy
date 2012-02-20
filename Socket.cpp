#include "Socket.h"

#include <cassert>

Socket::Socket(bool IPV6, bool TCP)
{
	this->socket = ::socket((IPV6 ? PF_INET6 : PF_INET), (TCP ? SOCK_STREAM : SOCK_DGRAM), 0);
}

Socket::Socket(Domain domain, Type type, Protocol protocol)
{
	this->socket = ::socket(domain, type, protocol);
}

bool Socket::bind(SocketAddress addr)
{
	return ::bind(this->socket, (const sockaddr*)&addr.saddr, sizeof(addr.saddr)) == 0;
}

bool Socket::listen(unsigned int max)
{
	return ::listen(this->socket, max) == 0;
}

Socket Socket::accept(SocketAddress* addr)
{
socket_t nsock;

	if(addr) {
		SocketAddress naddr;
		int nlen = sizeof(naddr.saddr);
		nsock = ::accept(this->socket, (sockaddr*)&naddr.saddr, &nlen);
		*addr = naddr;
	} else {
		nsock = ::accept(this->socket, NULL, 0);
	}
	return Socket(nsock);
}

bool Socket::connect(SocketAddress addr)
{
	return ::connect(this->socket, (const sockaddr*)&addr.saddr, sizeof(addr.saddr)) == 0;
}

int Socket::recv(char* buf, size_t max_size, bool force)
{
	assert(buf != NULL);

	int read = ::recv(this->socket, buf, max_size, 0);

	if(force) {
		int total = read;
		while(read > 0 && total < max_size) {
			read = ::recv(this->socket, buf + total, max_size - total, 0);
			total += read;
		}
		return total;
	}
	return read;
}

size_t Socket::send(const char* buf, size_t size)
{
	assert(buf != NULL);

	size_t total = 0;
	while(total < size) {
		int sent = ::send(this->socket, buf + total, size-total, 0);
		if(sent == SOCKET_ERROR) {
			break;
		}
		total += sent;
	}
	return total;
}

bool Socket::select_read(long seconds, long microseconds) const
{
	fd_set wait;
	FD_ZERO(&wait);
	FD_SET(this->socket, &wait);

	const timeval* timeout = NULL; // infinite
	timeval s_timeout;

	if(seconds >= 0)
	{
		s_timeout.tv_sec = seconds;
		s_timeout.tv_usec = microseconds;
		timeout = &s_timeout;
	}

	int ret = ::select(this->socket + 1, &wait, NULL, NULL, timeout);

	return ret == 1; // 0 = timeout, < 0 = error, > 0 = amount ready
}

bool Socket::shutdown(bool receive, bool send)
{
	assert(receive || send);

	int how;
	if(receive && !send) {
		how = 0;
	} else if (!receive && send) {
		how = 1;
	} else {
		how = 2;
	}

	return ::shutdown(this->socket, how) == 0;
}

bool Socket::close()
{
	this->shutdown(true, true);
	return ::closesocket(this->socket) == 0;
}

bool Socket::startup()
{
#ifdef _WIN32
	const WORD WSVERSION = MAKEWORD(2,2); // Win98+
	WSADATA wsad;
	return ::WSAStartup(WSVERSION, &wsad) == 0;
#else
	return true;
#endif
}

bool Socket::unload()
{
#ifdef _WIN32
	return ::WSACleanup() == 0;
#else
	return true;
#endif
}

Address::Address()
{
	this->a.addr4.s_addr = INADDR_ANY;
}

Address::Address(in_addr addr4)
{
	this->a.addr4 = addr4;
}

Address::Address(in6_addr addr6)
{
	this->a.addr6 = addr6;
}

Address::operator in_addr() const
{
	return this->a.addr4;
}

Address::operator in6_addr() const
{
	return this->a.addr6;
}

string Address::getHostName()
{
const size_t MAX_HOSTNAME = 255;
char localHostName[MAX_HOSTNAME];
string out;

	if(gethostname(localHostName, sizeof(localHostName)) == 0)
	{
		out = localHostName;
	}
	return out;
}

//IPv4 only :s
Address Address::fromHost(string host)
{
Address out;

	const hostent* local_ent = gethostbyname(host.c_str());
	if(local_ent)
	{
		const in_addr* p_addr = (in_addr*)local_ent->h_addr;
		if(p_addr)
			out = Address(*p_addr);
	}
	return out;
}

Address Address::fromPresentation(string presentation)
{
	return Address();
}

string Address::toPresentation() const
{
	return inet_ntoa(this->a.addr4);
}

SocketAddress::SocketAddress(Family family, Address address, port_t port)
{
	memset(&this->saddr, 0, sizeof(this->saddr));

	switch(family) {
		case INET:
			{
			sockaddr_in* dest = (sockaddr_in*)&this->saddr;
			dest->sin_family = AF_INET;
			dest->sin_addr = (in_addr)address;
			dest->sin_port = htons(port);
			break;
			}
		case INET6:
			{
			sockaddr_in6* dest = (sockaddr_in6*)&this->saddr;
			dest->sin6_family = AF_INET6;
			dest->sin6_addr = (in6_addr)address;
			dest->sin6_port = htons(port);
			break;
			}
	}
}

SocketAddress::SocketAddress(sockaddr_in saddr4)
{
	*(sockaddr_in*)&this->saddr = saddr4;
}

SocketAddress::SocketAddress(sockaddr_in6 saddr6)
{
	*(sockaddr_in6*)&this->saddr = saddr6;
}

SocketAddress::Family SocketAddress::getFamily() const
{
	switch(this->saddr.ss_family) {
		case AF_INET:
			return INET;
		case AF_INET6:
			return INET6;
	}
}

Address SocketAddress::getAddress() const
{
	switch(this->saddr.ss_family) {
		case AF_INET:
			return Address(((sockaddr_in*)&this->saddr)->sin_addr);
		case AF_INET6:
			return Address(((sockaddr_in6*)&this->saddr)->sin6_addr);
	}
}

SocketAddress::port_t SocketAddress::getPort() const
{
	switch(this->saddr.ss_family) {
		case AF_INET:
			return ntohs(((sockaddr_in*)&this->saddr)->sin_port);
		case AF_INET6:
			return ntohs(((sockaddr_in6*)&this->saddr)->sin6_port);
	}
}

SocketAddress::operator sockaddr_in() const
{
	if(this->saddr.ss_family != AF_INET) {
		throw "invalid cast to IPV4 SocketAddress";
	}
	return *(sockaddr_in*)&this->saddr;
}

SocketAddress::operator sockaddr_in6() const
{
	if(this->saddr.ss_family != AF_INET6) {
		throw "invalid cast to IPV6 SocketAddress";
	}
	return *(sockaddr_in6*)&this->saddr;
}
