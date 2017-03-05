#include "stream.hpp"
#include <vector>

void copy(ostream & out, istream & in, size_t bufsize)
{
	std::vector<char> buf(bufsize);
	copy(out, in, buf.data(), buf.size());
}

void copy(ostream & out, istream & in, char * buf, size_t bufsize)
{
	for (;;)
	{
		size_t r = in.read(buf, bufsize);
		if (r == 0)
			break;
		out.write_all(buf, r);
	}
}
