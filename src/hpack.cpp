#include "hpack.hpp"
#include "hpack_unhuff.hpp"

static header_view const g_static_table[] = {
	{ ":authority" },
	{ ":method", "GET" },
	{ ":method", "POST" },
	{ ":path", "/" },
	{ ":path", "/index.html" },
	{ ":scheme", "http" },
	{ ":scheme", "https" },
	{ ":status", "200" },
	{ ":status", "204" },
	{ ":status", "206" },
	{ ":status", "304" },
	{ ":status", "400" },
	{ ":status", "404" },
	{ ":status", "500" },
	{ "accept-charset" },
	{ "accept-encoding", "gzip, deflate" },
	{ "accept-language" },
	{ "accept-ranges" },
	{ "accept" },
	{ "access-control-allow-origin" },
	{ "age" },
	{ "allow" },
	{ "authorization" },
	{ "cache-control" },
	{ "content-disposition" },
	{ "content-encoding" },
	{ "content-language" },
	{ "content-length" },
	{ "content-location" },
	{ "content-range" },
	{ "content-type" },
	{ "cookie" },
	{ "date" },
	{ "etag" },
	{ "expect" },
	{ "expires" },
	{ "from" },
	{ "host" },
	{ "if-match" },
	{ "if-modified-since" },
	{ "if-none-match" },
	{ "if-range" },
	{ "if-unmodified-since" },
	{ "last-modified" },
	{ "link" },
	{ "location" },
	{ "max-forwards" },
	{ "proxy-authenticate" },
	{ "proxy-authorization" },
	{ "range" },
	{ "referer" },
	{ "refresh" },
	{ "retry-after" },
	{ "server" },
	{ "set-cookie" },
	{ "strict-transport-security" },
	{ "transfer-encoding" },
	{ "user-agent" },
	{ "vary" },
	{ "via" },
	{ "www-authenticate" },
};

static size_t const g_static_table_size = std::size(g_static_table);

hpack_dynamic_table::hpack_dynamic_table()
	: table_size_(0), table_capacity_(0)
{
}

void hpack_dynamic_table::resize(size_t capacity)
{
	// XXX
}

size_t hpack_dynamic_table::size() const
{
	return table_.size();
}

header_view hpack_dynamic_table::operator[](size_t index) const
{
	auto && e = table_[index];
	return{ e.name, e.value };
}

void hpack_dynamic_table::add(std::string name, std::string value)
{
	size_t new_entry_size = name.size() + value.size() + 32;
	if (new_entry_size > table_capacity_)
	{
		table_size_ = 0;
		table_.clear();
		return;
	}

	table_.push_back({ std::move(name), std::move(value) });
	table_size_ += new_entry_size;

	while (table_size_ > table_capacity_)
	{
		auto && e = table_.front();
		table_size_ -= e.name.size() + e.value.size() + 32;
		table_.pop_front();
	}
}

hpack_decoder::hpack_decoder(size_t max_cap)
	: table_max_capacity_(max_cap)
{
}

template <int prefix_len, typename Integral>
static char const * read_int(Integral & val, char const * first, char const * last)
{
	// XXX: handle overflows

	static uint8_t const mask = (1<<prefix_len) - 1;

	if (first == last)
		return nullptr;

	uint8_t prefix = *first++ & mask;
	if (prefix != mask)
	{
		val = prefix;
		return first;
	}

	Integral r = 0;
	while (first != last)
	{
		uint8_t ch = *first++;

		r = (r << 7) | (ch & 0x7f);

		if ((ch & 0x80) == 0)
		{
			val = r + mask;
			return first;
		}
	}

	return nullptr;
}

static char const * read_string(std::string & str, char const * first, char const * last)
{
	if (first == last)
		return nullptr;

	bool huffman = (*first & 0x80) != 0;

	uint32_t len;
	first = read_int<7>(len, first, last);
	if (!first)
		return nullptr;

	if (last - first < len)
		return nullptr;

	last = first + len;

	if (!huffman)
	{
		str.assign(first, len);
		return last;
	}

	str.clear();

	uint8_t flags = hpack_unhuff_entry::valid | hpack_unhuff_entry::last;
	uint8_t state = 0;

	auto decode_one = [&](uint8_t value) {
		auto && entry = g_hpack_unhuff_table[state][value];

		state = entry.next_state;
		flags = entry.flags;
		if (flags & hpack_unhuff_entry::decodes)
			str.push_back(entry.value);

		return flags & hpack_unhuff_entry::valid;
	};


	while (first != last)
	{
		uint8_t ch = *first++;
		if (!decode_one(ch >> 4) || !decode_one(ch & 0xf))
			return nullptr;
	}

	if (flags & hpack_unhuff_entry::last)
		return last;

	return nullptr;
}

bool hpack_decoder::decode(std::vector<header> & headers, std::string_view buf)
{
	char const * first = buf.begin();
	char const * last = buf.end();

	auto getch = [&] {
		uint8_t r = (uint8_t)buf[0];
		buf.remove_prefix(1);
		return r;
	};

	while (first != last)
	{
		if (*first & 0x80)
		{
			uint32_t idx = 0;
			first = read_int<7>(idx, first, last);
			if (!first)
				return false;

			if (idx == 0 || idx > this->entry_count())
				return false;

			headers.push_back(this->get_entry(idx));
		}
		else if (*first & 0x40)
		{
			uint32_t idx = 0;
			first = read_int<6>(idx, first, last);
			if (!first)
				return false;

			if (idx > this->entry_count())
				return false;

			if (idx != 0)
			{
				std::string value;
				first = read_string(value, first, last);
				if (!first)
					return false;

				header_view hv = this->get_entry(idx);
				dynamic_table_.add(hv.name, value);
				headers.push_back({ hv.name, std::move(value) });
			}
			else
			{
				std::string name;
				first = read_string(name, first, last);
				if (!first)
					return false;

				std::string value;
				first = read_string(value, first, last);
				if (!first)
					return false;

				dynamic_table_.add(name, value);
				headers.push_back({ std::move(name), std::move(value) });
			}
		}
		else if (*first & 0x20)
		{
			uint32_t cap = 0;
			first = read_int<6>(cap, first, last);
			if (!first)
				return false;

			if (cap > table_max_capacity_)
				return false;

			dynamic_table_.resize(cap);
		}
		else
		{
			uint32_t idx;
			first = read_int<4>(idx, first, last);
			if (!first)
				return false;

			if (idx > this->entry_count())
				return false;

			if (idx != 0)
			{
				std::string value;
				first = read_string(value, first, last);
				if (!first)
					return false;

				header_view hv = this->get_entry(idx);
				headers.push_back({ hv.name, std::move(value) });
			}
			else
			{
				std::string name;
				first = read_string(name, first, last);
				if (!first)
					return false;

				std::string value;
				first = read_string(value, first, last);
				if (!first)
					return false;

				headers.push_back({ std::move(name), std::move(value) });
			}
		}
	}

	return true;
}

size_t hpack_decoder::entry_count() const
{
	return g_static_table_size + dynamic_table_.size();
}

header_view hpack_decoder::get_entry(size_t index) const
{
	if (index > g_static_table_size)
		return dynamic_table_[index - g_static_table_size - 1];
	else
		return g_static_table[index - 1];
}
