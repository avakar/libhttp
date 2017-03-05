#ifndef HPACK_HPP
#define HPACK_HPP

#include "http_server.hpp"
#include <deque>

struct hpack_dynamic_table
{
	explicit hpack_dynamic_table();

	void resize(size_t capacity);
	size_t size() const;
	header_view operator[](size_t index) const;
	void add(std::string name, std::string value);

private:
	struct entry
	{
		std::string name;
		std::string value;
	};

	std::deque<entry> table_;
	size_t table_size_;
	size_t table_capacity_;
};

struct hpack_decoder
{
	explicit hpack_decoder(size_t max_cap);

	bool decode(std::vector<header> & headers, std::string_view buf);

private:
	size_t entry_count() const;
	header_view get_entry(size_t index) const;

	hpack_dynamic_table dynamic_table_;
	size_t table_max_capacity_;
};

#endif // HPACK_HPP
