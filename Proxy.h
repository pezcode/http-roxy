#ifndef PROXY_H
#define PROXY_H

#pragma once

#include <queue>
#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>
#include "semaphore.hpp"
#include "Socket.h"
#include <http.hpp>

class Proxy
{
public:

	Proxy(SocketAddress::port_t port);

	// our server listening for connection attempts
	bool listen(unsigned int max_incoming = 5);
	void interrupt();

private:

	static const unsigned int THREADS = 1U;
	static const unsigned int KEEPALIVE_TIMEOUT = 5U; // seconds

	SocketAddress::port_t port;
	bool stop_listening;

	std::queue<Socket> incoming_connections;
	boost::mutex incoming_guard;
	semaphore incoming_indicator;

	bool thread_handle_connection(int tid);

	std::string receive_request(http::Request& request, Socket socket);
	bool forward_request(const std::string& request, Socket from, Socket to);
	std::string receive_response(http::Response& response, Socket socket);
	bool forward_response(const std::string& response, Socket from, Socket to);

	void enqueue_incoming(Socket socket);
	Socket request_incoming();
	void close_unhandled_incoming();
};

#endif
