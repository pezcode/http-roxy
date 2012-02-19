#include "Message.h"

#include <iostream>

void Message::info(const std::string& str)
{
	info() << str << std::endl;
}

void Message::warning(const std::string& str)
{
	warning() << str << std::endl;
}

void Message::error(const std::string& str)
{
	error() << str << std::endl;
}

std::ostream& Message::info()
{
	std::cout << "[i] ";
	return std::cout;
}

std::ostream& Message::warning()
{
	std::cout << "[!] ";
	return std::cout;
}

std::ostream& Message::error()
{
	std::cerr << "[!] ";
	return std::cerr;
}
