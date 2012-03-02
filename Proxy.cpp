#include "Proxy.h"

#include <iostream>
#include <string>
#include <cassert>
#include "Message.h"

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

	SocketAddress server_addr(SocketAddress::INET, Address() /*INADDR_ANY*/, this->port);

	// Assign address and port to socket
	if(!s_server.bind(server_addr))
	{
		Message::error() << "cannot bind to port" << '\n';
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
	for(int i = 0; i < max_incoming; i++)
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

		http::Request request;
		http::Response response;

		bool keep_alive = false;

		do
		{
			std::string request_header = this->receive_message_header(request, s_client);
			if(!request.headers_complete())
			{
				Message::error() << "Invalid request header" << '\n';
				//std::ofstream file("invalid_request.txt");
				//file << request_header << std::endl;
				break;
			}

			if(!s_server.valid())
			{
				string host = extract_host(request);
				s_server = connect(host);
				if(!s_server.valid())
				{
					Message::error() << "Can't connect to host " << host << '\n';
					break;
				}
			}
			
			if(!this->forward_message(request_header, request, s_client, s_server))
			{
				Message::error() << "Forwarding request failed" << '\n';
				break;
			}

			if(request.upgrade())
			{
				// protocol update on the same port
				// ???
			}

			if(request.method() == http::Method::connect())
			{
				// connection on another port
				// ???
			}

			bool client_keepalive = (request.flags() & http::Flags::keepalive()) != 0;
			if(!client_keepalive)
			{
				s_server.shutdown(false, true); // signal EOF (we're done writing)
			}

			std::string response_header = this->receive_message_header(response, s_server);
			if(!response.headers_complete())
			{
				Message::error() << "Invalid response header" << '\n';
				//std::ofstream file("invalid_response.txt");
				//file << request_header << std::endl;
				break;
			}

			if(request.method() != http::Method::head()) //if(!(request.flags() & http::Flags::skipbody()))
			{
				if(!this->forward_message(response_header, response, s_server, s_client))
				{
					Message::error() << "Forwarding response failed" << '\n';
					break;
				}
			}

			bool server_keepalive = (response.flags() & http::Flags::keepalive()) != 0;
			keep_alive = client_keepalive && server_keepalive;
		}
		while (keep_alive);

		s_server.close();
		s_client.close();
	}

	return true;
}

std::string Proxy::extract_host(const http::Request& request)
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

Socket Proxy::connect(string host)
{
	Socket socket;

	http::Url url(host);
	//if(url.has_host()) // somehow this doesn't work for URLs without schema
	{
		SocketAddress::port_t port = 80;
		if(url.has_port())
		{
			port = atoi(url.port().c_str());
		}

		Address addr = Address::fromHost(host);
		if(!addr.isAny())
		{
			socket = Socket(Socket::INET, Socket::STREAM);
			SocketAddress sock_addr(SocketAddress::INET, addr, port);
			if(!socket.connect(sock_addr))
			{
				socket.close();
			}
		}
	}

	return socket;
}

std::string Proxy::receive_message_header(http::Message& message, Socket socket)
{
	assert(socket.valid());

	std::string content;

	message.clear();

	if(!socket.select_read(KEEPALIVE_TIMEOUT))
	{
		Message::error() << "select failed" << '\n';
		return content;
	}

	const size_t BUF_SIZE = 4096;
	char buf[BUF_SIZE];

	do 
	{
		int read = socket.recv(buf, sizeof(buf), Socket::PEEK);
		if(read < 0)
		{
			Message::error() << "recv < 0" << '\n';
			break;
		}

		size_t parsed = 0;

		try
		{
			parsed = message.feed(buf, read);
		}
		catch (const http::Error& e)
		{
			Message::error() << e.what() << '\n';
			break;
		}

		if(parsed == 0)
		{
			// EOF (read is 0)
			Message::warning() << "eof" << '\n';
			break;
		}

		socket.recv(buf, parsed); // remove from queue
		content.append(buf, parsed);
	}
	while(!message.headers_complete());

	return content;
}

bool Proxy::forward_message(const std::string& header, http::Message& message, Socket from, Socket to)
{
	assert(header.length() > 0);
	assert(message.headers_complete());
	assert(from.valid());
	assert(to.valid());

	if(to.send(header.data(), header.size()) != header.size())
	{
		return false;
	}

	const size_t BUF_SIZE = 4096;
	char buf[BUF_SIZE];

	while(!message.complete())
	{
		int read = from.recv(buf, sizeof(buf), Socket::PEEK);
		if(read < 0)
		{
			Message::error() << "recv < 0" << '\n';
			break;
		}

		size_t parsed = 0;

		try
		{
			parsed = message.feed(buf, read);
		}
		catch(const http::Error& e)
		{
			Message::error() << e.what() << '\n';
			break;
		}

		if(parsed < read && message.complete())
		{
			Message::error() << "read too much" << '\n';
			break;
		}

		if(parsed == 0)
		{
			// EOF (read is 0)
			Message::warning() << "eof" << '\n';
			break;
		}

		from.recv(buf, parsed);
		to.send(buf, parsed);
	}

	return message.complete();
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
