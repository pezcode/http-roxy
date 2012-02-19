#include "Proxy.h"

#include <iostream>
#include <string>
#include <queue>
#include <cstring>
#include <cassert>

#include "Socket.h"
#include "Message.h"

#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>
#include "semaphore.hpp"

#include <Request.hpp>

using std::string;
using std::queue;

const size_t MAX_THREADS = 4;

queue<Socket> socketQueue;
boost::mutex queueGuard;
semaphore queueIndicator;
bool ctrlc_pressed = false;

// HTTP stuff
bool receiveHeader(Socket input, string& header, string& rest);
size_t bodySizeFromHeader(const string& header);
bool hostFromHeader(const string& request, string& host, string& port);

bool isConnectionAlive(const string& header);
bool forwardRequest(const string& url, SocketAddress::port_t, const string& request, Socket output);

// the threads that maintain and tunnel existing connections
bool threadHandleRequest(int tid);

bool receiveHeader(Socket input, string& header, string& rest)
{
const string TERMINATOR = "\r\n\r\n";
const size_t BUFSIZE = 2048;
char buf[BUFSIZE];
string data = "";
bool found = false;

	while(!found)
	{
		int n_read = input.recv(buf, sizeof(buf));
		if(n_read < 1) // error or connection closed
		{
			return false;
		}
		data += string(buf, n_read);
		size_t len;
		if((len = data.find(TERMINATOR)) != string::npos)
		{
			header = data.substr(0, len + TERMINATOR.length());
			rest = data.substr(len + TERMINATOR.length(), string::npos);
			found = true;
		}
	}
	return true;
}

size_t bodySizeFromHeader(const string& header)
{
const char CONTENT_LENGTH[] = "Content-Length:";

	const char* buf = header.c_str();
	const char* found = strstr(buf, CONTENT_LENGTH);
	if((found == buf) || ((found >= buf+2) && !strncmp(found-2, "\r\n", 2)))
	{
		found += sizeof(CONTENT_LENGTH)-1;
		while(isspace(*found))
			found++;

		return atoi(found);
	}
	return 0;
}

bool hostFromHeader(const string& header, string& host, string& port)
{
const char HOST[] = "Host:";

	host = "";
	port = "";

	const char* buf = header.c_str();
	const char* request = strstr(buf, HOST);
	if((request == buf) || ((request >= buf+2) && !strncmp(request-2, "\r\n", 2)))
	{
		request += sizeof(HOST)-1;

		while(isspace(*request))
			request++;

		while(*request && !isspace(*request)) //find host address
		{
			if(*request == ':') //find port number
			{
				request++;
				while(isdigit(*request))
				{
					port += *request;
					request++;
				}
				break;
			}
			host += *request;
			request++;
		}
	}

	if(!host.empty() && port.empty()) {
		port = "80";
	}

	return !host.empty();
}

bool isConnectionAlive(const string& header)
{
const char CONNECTION[] = "Connection:";
const char CLOSE[] = "Close";

	const char* buf = header.c_str();
	const char* found = strstr(buf, CONNECTION);
	if((found == buf) || ((found >= buf+2) && !strncmp(found-2, "\r\n", 2)))
	{
		found += sizeof(CONNECTION)-1;
		while(isspace(*found))
			found++;
		return 0 != strncmp(found, CLOSE, sizeof(CLOSE)-1);
	}
	return true;
}

bool Proxy::thread_HandleRequest(int tid)
{
	while(true)
	{
		// wait for a semaphore counter > 0 and automatically decrease the counter
		try
		{
			queueIndicator.wait();
		}
		catch(boost::thread_interrupted)
		{
			break;
		}

		boost::unique_lock<boost::mutex> lock(queueGuard);

		assert(!socketQueue.empty());

		Socket s_client = socketQueue.front();
		socketQueue.pop();

		lock.unlock();

		assert(s_client.valid());

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

	string mode;
	timeval s_timeout;
	const timeval* timeout;
	fd_set wait;
	FD_ZERO(&wait);
	FD_SET(s_request.get(), &wait);

	if(keepAlive)
	{
		mode = "keep-alive";
		s_timeout.tv_sec = 3; // 3 seconds
		s_timeout.tv_usec = 0;
		timeout = &s_timeout;
	}
	else
	{
		mode = "closed";
		timeout = NULL; // infinite
	}

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
		fd_set temp = wait;
		int ret = select(s_request.get()+1/*WTF*/, &temp, NULL, NULL, timeout);
		if(ret == SOCKET_ERROR)
		{
			Message::error() << "select failed" << '\n';
			break;
		}
		else if(ret == 1) // ready to recv
		{
			assert(FD_ISSET(s_request, &temp));

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
}

bool Proxy::run()
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

	//test
	http::Request request;
	request.clear();

	std::cout << "Listening at " << hostIP << ":" << this->port << '\n';
	std::cout << "CTRL+C to exit" << '\n' << '\n';

	boost::thread_group threads;
	for(int i = 0; i < MAX_THREADS; i++)
	{
		threads.create_thread(boost::bind(&Proxy::thread_HandleRequest, this, i+1));
	}

	while(!ctrlc_pressed)
	{
		// wait for incoming connections and pass them to the worker threads
		Socket s_connection = s_server.accept();
		if(s_connection.valid())
		{
			boost::unique_lock<boost::mutex> lock(queueGuard);
			socketQueue.push(s_connection);
			queueIndicator.signal();
		}
	}

	threads.interrupt_all(); // interrupt threads at queueGuard.wait()
	threads.join_all();      // wait...

	s_server.close();
	return true;
}
