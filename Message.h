#include <string>
#include <ostream>

class Message
{
public:

	static void info(const std::string& str);
	static void warning(const std::string& str);
	static void error(const std::string& str);

	static std::ostream& info();
	static std::ostream& warning();
	static std::ostream& error();
};
