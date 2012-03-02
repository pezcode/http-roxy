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
	bool listen(unsigned int max_incoming = 4);
	void interrupt();

private:

	static const unsigned int KEEPALIVE_TIMEOUT = 5U; // seconds

	SocketAddress::port_t port;
	bool stop_listening;

	std::queue<Socket> incoming_connections;
	boost::mutex incoming_guard;
	semaphore incoming_indicator;

	bool thread_handle_connection(int tid);

	static std::string extract_host(const http::Request& request);
	static Socket connect(string host);

	static std::string receive_message_header(http::Message& message, Socket socket);
	static bool forward_message(const std::string& header, http::Message& message, Socket from, Socket to);

	void enqueue_incoming(Socket socket);
	Socket request_incoming();
	void close_unhandled_incoming();
};

#endif
