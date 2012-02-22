This is a very simple HTTP web proxy written in C++.
It's written for giggles, with code readability in mind.
Please don't expect an efficient or bugfree experience, it
was built for learning purposes.

The code uses:

- Berkeley sockets for TCP communication
- boost (http://www.boost.org/) for threading
- httpxx (https://github.com/AndreLouisCaron/httpxx) for parsing
  the HTTP headers
