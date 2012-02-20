#include <queue>
#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>
#include "semaphore.hpp"
#include "Socket.h"

class Proxy
{
public:

	Proxy(SocketAddress::port_t port);

	// our server listening for connection attempts
	bool listen(unsigned int max_incoming = 5);
	void interrupt();

private:

	static const size_t THREADS = 4;

	SocketAddress::port_t port;
	bool stop_listening;

	std::queue<Socket> incoming_connections;
	boost::mutex incoming_guard;
	semaphore incoming_indicator;

	bool thread_handle_request(int tid);

	void enqueue_incoming(const Socket& socket);
	Socket request_incoming();
	void close_unhandled_incoming();
};
