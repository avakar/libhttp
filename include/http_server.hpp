#ifndef HTTP_SERVER_HPP
#define HTTP_SERVER_HPP

#include "stream.hpp"
#include <string_view>
#include <vector>
#include <memory>
#include <functional>

int compare_header_name(std::string_view lhs, std::string_view rhs) noexcept;

struct header_view
{
	std::string_view name;
	std::string_view value;

	bool operator<(header_view const & rhs)
	{
		return compare_header_name(name, rhs.name) < 0;
	}

	friend bool operator<(header_view const & lhs, std::string_view const & rhs)
	{
		return compare_header_name(lhs.name, rhs) < 0;
	}

	friend bool operator<(std::string_view const & lhs, header_view const & rhs)
	{
		return compare_header_name(lhs, rhs.name) < 0;
	}
};

struct header
{
	std::string name;
	std::string value;

	header(header_view hv)
		: name(hv.name), value(hv.value)
	{
	}

	header(std::string name, std::string value)
		: name(std::move(name)), value(std::move(value))
	{
	}
};

using header_list = std::vector<header_view>;

std::pair<header_view const *, header_view const *> get_header_range(header_list const & headers, std::string_view name);
std::string_view const * get_single(header_list const & headers, std::string_view name);

struct enum_headers
{
	struct const_iterator
	{
		const_iterator(enum_headers * self)
			: self_(self)
		{
		}

		std::string_view operator*() const
		{
			return self_->front();
		}

		void operator++()
		{
			self_->pop_front();
		}

		friend bool operator==(const_iterator const & lhs, const_iterator const & rhs)
		{
			return (lhs.self_ == rhs.self_)
				|| ((lhs.self_ == nullptr || lhs.self_->empty()) == (rhs.self_ == nullptr && rhs.self_->empty()));
		}

		friend bool operator!=(const_iterator const & lhs, const_iterator const & rhs)
		{
			return !(lhs == rhs);
		}

		enum_headers * self_;
	};

	explicit enum_headers(header_list const & h, std::string_view name)
	{
		auto r = get_header_range(h, name);
		first_ = r.first;
		last_ = r.second;
	}

	bool empty() const
	{
		return first_ != last_;
	}

	std::string_view front() const
	{
		assert(!this->empty());
		return first_->value;
	}

	void pop_front()
	{
		assert(!this->empty());
		++first_;
	}

	const_iterator begin()
	{
		return const_iterator(this);
	}

	const_iterator end()
	{
		return const_iterator(nullptr);
	}

private:
	header_view const * first_;
	header_view const * last_;
};

struct request
{
	std::string_view method;
	std::string_view path;
	header_list headers;
	std::shared_ptr<istream> body;
};

struct response
{
	uint16_t status_code;
	std::string status_text;
	std::vector<header> headers;

	uint64_t content_length;
	std::shared_ptr<istream> body;

	response(uint16_t status_code, std::initializer_list<header> headers = {})
		: status_code(status_code), headers(headers), content_length(0)
	{
	}

	response(std::shared_ptr<istream> body, std::initializer_list<header> headers, uint16_t status_code = 200)
		: status_code(status_code), headers(headers), content_length(uint64_t(-1)), body(body)
	{
	}

	response(std::string body, std::initializer_list<header> headers = {{ "content-type", "text/plain" }}, uint16_t status_code = 200)
		: status_code(status_code), headers(headers), content_length(body.size())
	{
		struct x
		{
			std::string str_;
			string_istream ss_;

			explicit x(std::string && str)
				: str_(std::move(str)), ss_(str_)
			{
			}
		};

		auto px = std::make_shared<x>(std::move(body));
		this->body = std::shared_ptr<istream>(px, &px->ss_);
	}

	response(std::string_view body, std::initializer_list<header> headers = {{ "content-type", "text/plain" }}, uint16_t status_code = 200)
		: response(std::string(body), headers, status_code)
	{
	}

	response(char const * body, std::initializer_list<header> headers = {{ "content-type", "text/plain" }}, uint16_t status_code = 200)
		: response(std::string(body), headers, status_code)
	{
	}
};

response http_abort(uint16_t status_code);

void http_server(istream & in, ostream & out, std::function<response(request &&)> const & fn);
void http2_server(istream & in, ostream & out, std::function<response(request &&)> const & fn);

#endif // HTTP_SERVER_HPP
