#include "Authentication.h"

#include "base64.h"

Authentication::Authentication(const std::string& name, const std::string& password)
{
	this->name = name;
	this->password = password;
	this->valid = true;
}

Authentication::Authentication(const std::string& line)
{
	this->valid = false;

	size_t pos = line.find("Basic ");
	if(pos == 0)
	{
		std::string decoded = base64_decode(line.substr(pos + 6, std::string::npos));
		pos = decoded.find(':');
		if(pos != std::string::npos)
		{
			this->name = decoded.substr(0, pos);
			this->password = decoded.substr(pos + 1, std::string::npos);
			this->valid = true;
		}
	}
}

bool Authentication::operator==(const Authentication& other) const
{
	if(!this->valid)
		return false;

	return this->name     == other.name &&
	       this->password == other.password;
}
