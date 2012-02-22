#include "Proxy.h"

#include <iostream>
#include <string>
#include <cassert>
#include "Message.h"

http::Request Proxy::receive_request(Socket socket)
{
	http::Request request;

	if(!socket.select_read(KEEPALIVE_TIMEOUT))
		return request;

	// How to deal with bytes already read?

	const size_t BUF_SIZE = 4096;
	char buf[BUF_SIZE];

	do 
	{
		int read = socket.recv(buf, sizeof(buf), Socket::PEEK);
		if(read < 0)
		{
			// error
			break;
		}

		size_t parsed = request.feed(buf, read);
		socket.recv(buf, parsed); // remove from queue

		if(parsed == 0)
		{
			// error or EOF
			break;
		}

		

	} while (!request.complete());

	return request;

	/*

		if(request.upgrade())
		{
			// protocol update on the same port
			// error
		}

		const http::Method method = request.method();
		string method_name = request.method_name();

		if(method == http::Method::connect()) // SSL
		{
			// error? tunnel tcp?
		}
	*/
}

bool Proxy::forward_request(const http::Request& request, Socket from, Socket to) // FIX!
{
	// build string from http::Request?

	return true;
}

http::Response Proxy::receive_response(Socket socket) // FIX!
{
	http::Response response;

	if(!socket.select_read(KEEPALIVE_TIMEOUT))
		return response;

	// How to deal with bytes already read?

	const size_t BUF_SIZE = 4096;
	char buf[BUF_SIZE];

	do 
	{
		int read = socket.recv(buf, sizeof(buf), Socket::PEEK);
		if(read < 0)
		{
			// error
			break;
		}

		size_t parsed = response.feed(buf, read);
		socket.recv(buf, parsed); // remove from queue

		if(parsed == 0)
		{
			// error or EOF
			break;
		}
	} while (!response.complete());

	return response;
}

bool Proxy::forward_response(const http::Response& response, Socket from, Socket to) // FIX!
{
	// build string from http::Response?

	return true;
}

void Proxy::enqueue_incoming(Socket socket)
{
	assert(socket.valid());

	boost::unique_lock<boost::mutex> lock(this->incoming_guard);

	this->incoming_connections.push(socket);
	this->incoming_indicator.signal();
}

Socket Proxy::request_incoming()
{
	// wait for a semaphore counter > 0 and automatically decrease the counter
	this->incoming_indicator.wait();

	boost::unique_lock<boost::mutex> lock(this->incoming_guard);

	assert(!this->incoming_connections.empty());

	Socket incoming = this->incoming_connections.front();
	this->incoming_connections.pop();

	assert(incoming.valid());

	return incoming;
}

void Proxy::close_unhandled_incoming()
{
	boost::unique_lock<boost::mutex> lock(this->incoming_guard);

	while(!this->incoming_connections.empty())
	{
		Socket socket = this->incoming_connections.front();
		this->incoming_connections.pop();
		socket.close();
	}
}

string extract_host(const http::Request& request)
{
	string host;

	if(request.has_header("Host"))
	{
		host = request.header("Host");
	}
	else
	{
		host = request.url();
	}

	return host;
}

Socket connect(const string& host)
{
	http::Url url(host);
	SocketAddress::port_t port = 80;
	if(url.has_port())
	{
		port = atoi(url.port().c_str());
	}

	Socket socket(Socket::INET, Socket::STREAM);
	SocketAddress addr(SocketAddress::INET, Address::fromHost(host), port);
	socket.connect(addr);

	return socket;
}

bool Proxy::thread_handle_connection(int tid)
{
	while(true)
	{
		Socket s_client, s_server;

		try
		{
			s_client = this->request_incoming();
		}
		catch(boost::thread_interrupted)
		{
			break;
		}

		bool keep_alive = false;

		do
		{
			http::Request request = this->receive_request(s_client);
			if(!request.complete())
			{
				// error
				break;
			}

			if(!s_server.valid())
			{
				string host = extract_host(request);
				s_server = connect(host);
			}
			
			this->forward_request(request, s_client, s_server);

			bool client_keepalive = (request.flags() & http::Flags::keepalive()) != 0;
			if(!client_keepalive)
			{
				s_server.shutdown(false, true); // signal EOF (we're done writing)
			}

			http::Response response = this->receive_response(s_server);
			if(!response.complete())
			{
				// error
				break;
			}

			this->forward_response(response, s_server, s_client);

			bool server_keepalive = (response.flags() & http::Flags::keepalive()) != 0;
			keep_alive = client_keepalive && server_keepalive;
		}
		while (keep_alive);

		s_server.close();
		s_client.close();
	}

	return true;
}

Proxy::Proxy(SocketAddress::port_t port)
{
	this->port = port;
	this->stop_listening = false;
}

bool Proxy::listen(unsigned int max_incoming)
{
	// Create server socket
	Socket s_server(Socket::INET, Socket::STREAM);
	if(!s_server.valid())
	{
		Message::error() << "invalid socket" << '\n';
		return false;
	}

	/*
	// load up address structs with getaddrinfo():
	addrinfo hints = { 0 };
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // fill in my ip
	
	// get local ip
	addrinfo* res;
	char buf[14];
	getaddrinfo(NULL, _itoa(port, buf, 10), &hints, &res);
	
	sockaddr_in serverAddr = { 0 };
	
	assert(res->ai_addrlen >= sizeof(serverAddr));

	serverAddr = *(sockaddr_in*)res->ai_addr;
	*/

	SocketAddress server_addr(SocketAddress::INET, Address() /*INADDR_ANY*/, this->port);

	// Assign address and port to socket
	if(!s_server.bind(server_addr))
	{
		Message::error() << "cannot bind to socket" << '\n';
		s_server.close();
		return false;
	}

	// Listen on port
	if(!s_server.listen(max_incoming))
	{
		Message::error() << "listen failed" << '\n';
		s_server.close();
		return false;
	}

	// Get local IP
	string hostIP = "localhost";
	string hostName = Address::getHostName();
	if(!hostName.empty())
	{
		Address host_addr = Address::fromHost(hostName);
		if(!host_addr.isAny())
		{
			hostIP = host_addr.toPresentation();
		}
	}

	std::cout << "Listening at " << hostIP << ":" << this->port << '\n';
	std::cout << "CTRL+C to exit" << '\n' << '\n';

	boost::thread_group threads;
	for(int i = 0; i < THREADS; i++)
	{
		threads.create_thread(boost::bind(&Proxy::thread_handle_connection, this, i+1));
	}

	while(!this->stop_listening)
	{
		// wait for incoming connections and pass them to the worker threads
		Socket s_connection = s_server.accept();
		if(s_connection.valid())
		{
			this->enqueue_incoming(s_connection);
		}
	}

	threads.interrupt_all(); // interrupt threads at request_incoming()
	threads.join_all();      // wait...

	this->close_unhandled_incoming();

	s_server.close();
	return true;
}

void Proxy::interrupt()
{
	this->stop_listening = true;
}
