#ifndef AUTHENTICATION_H
#define AUTHENTICATION_H

#pragma once

#include <string>

class Authentication
{
public:

	Authentication(const std::string& name, const std::string& password);
	Authentication(const std::string& line);

	bool operator==(const Authentication& other) const;

private:

	std::string name;
	std::string password;
	bool valid;
};

#endif
