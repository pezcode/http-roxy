#include "Proxy.h"

#include <iostream>
#include <string>
#include <cassert>

#include "Socket.h"
#include "Message.h"

#include "http.h"

bool receiveHeader(Socket input, string& header, string& rest);
bool forwardRequest(const string& url, SocketAddress::port_t, const string& request, Socket output);

bool receiveHeader(Socket input, std::string& header, std::string& rest)
{
const std::string TERMINATOR = "\r\n\r\n";
const size_t BUFSIZE = 2048;
char buf[BUFSIZE];
std::string data = "";
bool found = false;

	while(!found)
	{
		int n_read = input.recv(buf, sizeof(buf));
		if(n_read < 1) // error or connection closed
		{
			return false;
		}
		data += std::string(buf, n_read);
		size_t len;
		if((len = data.find(TERMINATOR)) != std::string::npos)
		{
			header = data.substr(0, len + TERMINATOR.length());
			rest = data.substr(len + TERMINATOR.length(), std::string::npos);
			found = true;
		}
	}
	return true;
}

void Proxy::enqueue_incoming(const Socket& socket)
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

bool Proxy::thread_handle_request(int tid)
{
	while(true)
	{
		Socket s_client;

		try
		{
			s_client = this->request_incoming();
		}
		catch(boost::thread_interrupted)
		{
			break;
		}

		string header, rest;
		if(receiveHeader(s_client, header, rest))
		{
			size_t to_read = bodySizeFromHeader(header);
			assert(to_read >= rest.size());
			to_read -= rest.size();

			const size_t BUFSIZE = 2048;
			char buf[BUFSIZE];

			bool error = false;
			size_t total = 0;
			while(total < to_read)
			{
				int n_read = s_client.recv(buf, min(to_read-total, sizeof(buf)));
				if(n_read < 1)
				{
					error = true;
					break;
				}
				rest += string(buf, n_read);
				total += n_read;
			}

			if(!error)
			{
				// we're done reading
				s_client.shutdown(true, false);

				string url, port;
				if(hostFromHeader(header, url, port))
				{
					forwardRequest(url, atoi(port.c_str()), header + rest, s_client);
				}
			}
		}
		s_client.close();
		Message::info() << "<thread " << tid << "> socket closed" << '\n';
	}
	return true;
}

bool forwardRequest(const string& url, SocketAddress::port_t port, const string& request, Socket s_output)
{
	assert(s_output.valid());

	// resolve host IP
	Address host_addr = Address::fromHost(url);
	if(host_addr.isAny())
	{
		Message::error() << "cannot resolve " << url << '\n';
		return false;
	}

	Socket s_request(Socket::INET, Socket::STREAM);
	if(!s_request.valid())
	{
		Message::error() << "socket error" << '\n';
		return false;
	}

	// connect to host
	SocketAddress client_addr(SocketAddress::INET, host_addr, port);
	if(!s_request.connect(client_addr))
	{
		Message::error() << "connect failed" << '\n';
		s_request.close();
		return false;
	}

	// send request to server
	const size_t size = request.size();
	if(s_request.send(request.c_str(), size) != size)
	{
		Message::error() << "send failed" << '\n';
		s_request.close();
		return false;
	}

	// we're done sending
	s_request.shutdown(false, true);

	// read incoming header
	string header, rest;
	if(!receiveHeader(s_request, header, rest))
	{
		Message::error() << "recv failed" << '\n';
		s_request.close();
		return false;
	}

	size_t to_read = bodySizeFromHeader(header);
	//assert(to_read >= rest.size());
	//to_read -= rest.size();

	bool keepAlive = isConnectionAlive(header);

	string mode = keepAlive ? "keep-alive" : "closed";

	Message::info() << "[i] " << "receiving (mode: " << mode << ")" << '\n';

	const size_t BUFSIZE = 8096;
	char recvbuf[BUFSIZE];
	string bufstr = header + rest;

	int n_read = 0;

	size_t total = 0;
	do
	{
		if(s_output.send(bufstr.c_str(), bufstr.size()) != bufstr.size())
		{
			Message::error() << "send failed" << '\n';
			return false;
		}

		// wait for more data to appear
		if(s_request.select_read(keepAlive ? -1 : 3)) // ready to recv
		{
			n_read = s_request.recv(recvbuf, sizeof(recvbuf));
			if(n_read == SOCKET_ERROR)			
			{
				Message::error() << "recv failed" << '\n';
				break;
			}
		}
		else
		{
			Message::warning() << "connection timed out" << '\n';
			break;
		}

		bufstr = string(recvbuf, n_read);
		total += n_read;
	}
	while(/*total < to_read && */n_read >= 1);

	Message::info() << "connection closed" << '\n';

	s_request.close();
	return true;
}

Proxy::Proxy(SocketAddress::port_t port)
{
	this->port = port;
	this->stop_listening = false;
}

bool Proxy::listen()
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
	if(!s_server.listen(5))
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
		threads.create_thread(boost::bind(&Proxy::thread_handle_request, this, i+1));
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

	threads.interrupt_all(); // interrupt threads at queueGuard.wait()
	threads.join_all();      // wait...

	s_server.close();
	return true;
}

void Proxy::interrupt()
{
	this->stop_listening = true;
}
