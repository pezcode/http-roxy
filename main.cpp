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

	std::vector<Authentication> auth;
	auth.push_back(Authentication("test-user", "test-password"));

	Proxy proxy(PROXY_PORT, auth);

	if(!proxy.listen())
	{
		Socket::unload();
		return EXIT_FAILURE;
	}

	Socket::unload();
	return EXIT_SUCCESS;
}
