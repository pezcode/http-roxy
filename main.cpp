#include <iostream>
#include "Proxy.h"
#include "Message.h"

int main(int argc, char* argv[])
{
	const char LOGO[] =
	" ______ ___ ___ ______ _______ \n"
	"|   __ \\   |   |   __ \\     __|\n"
	"|      <   |   |      <__     |\n"
	"|___|__|\\_____/|___|__|_______|";

	std::cout << LOGO << '\n' << '\n';

	const SocketAddress::port_t PROXY_PORT = 6666;

	if(!Socket::startup())
	{
		Message::error() << "init failed" << '\n';
		return EXIT_FAILURE;
	}

	Proxy proxy(PROXY_PORT);

	if(!proxy.listen())
	{
		Socket::unload();
		return EXIT_FAILURE;
	}

	Socket::unload();
	return EXIT_SUCCESS;
}
