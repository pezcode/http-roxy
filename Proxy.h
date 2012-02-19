#include "Socket.h"

class Proxy
{
public:

	Proxy(SocketAddress::port_t port);

	// our server listening for connection attempts
	bool run();

private:

	SocketAddress::port_t port;

	bool thread_HandleRequest(int tid);
};
