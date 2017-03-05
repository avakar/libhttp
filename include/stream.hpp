#ifndef STREAM_HPP
#define STREAM_HPP

#include <string_view>
#include <assert.h>

struct istream
{
	virtual size_t read(char * buf, size_t len) = 0;

	void read_all(char * buf, size_t len)
	{
		while (len)
		{
			size_t r = this->read(buf, len);
			assert(r <= len);

			if (r == 0)
				throw std::runtime_error("premature end of stream");

			buf += r;
			len -= r;
		}
	}

	std::string read_all()
	{
		std::string r;

		char buf[4096];
		for (;;)
		{
			size_t rd = this->read(buf, sizeof buf);
			if (rd == 0)
				break;
			r.append(buf, rd);
		}

		return r;
	}
};

struct ostream
{
	virtual size_t write(char const * buf, size_t len) = 0;

	virtual void close()
	{
	}

	void write_all(char const * buf, size_t len)
	{
		while (len)
		{
			size_t r = this->write(buf, len);
			assert(r != 0);
			assert(r <= len);

			buf += r;
			len -= r;
		}
	}

	void write_all(std::string_view str)
	{
		this->write_all(str.begin(), str.size());
	}
};

struct string_istream
	: istream
{
	explicit string_istream(std::string_view str)
		: str_(str)
	{
	}

	size_t read(char * buf, size_t len) override
	{
		if (len > str_.size())
			len = str_.size();
		memcpy(buf, str_.data(), len);
		return len;
	}

private:
	std::string_view str_;
};

void copy(ostream & out, istream & in, size_t bufsize = 64 * 1024);
void copy(ostream & out, istream & in, char * buf, size_t bufsize = 64 * 1024);

#endif // STREAM_HPP
