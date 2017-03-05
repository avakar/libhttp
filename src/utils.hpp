#ifndef UTILS_HPP
#define UTILS_HPP

#include <string_view>
#include <vector>

std::vector<std::string> split(std::string_view str, char sep);

bool starts_with(std::string_view str, std::string_view prefix);

template <typename T, typename Traits>
std::basic_string_view<T, Traits> lstrip(std::basic_string_view<T, Traits> const & str)
{
	auto first = str.begin();
	auto last = str.end();

	for (; first != last; ++first)
	{
		if (*first != ' ' && *first != '\t')
			return std::basic_string_view<T, Traits>(first, last - first);
	}

	return std::basic_string_view<T, Traits>();
}

template <typename T, typename Traits>
std::basic_string_view<T, Traits> rstrip(std::basic_string_view<T, Traits> const & str)
{
	auto first = str.begin();
	auto last = str.end();

	for (; first != last; --last)
	{
		auto ch = last[-1];
		if (ch != ' ' && ch != '\t')
			return std::basic_string_view<T, Traits>(first, last - first);
	}

	return std::basic_string_view<T, Traits>();
}

template <typename T, typename Traits>
std::basic_string_view<T, Traits> strip(std::basic_string_view<T, Traits> const & str)
{
	return rstrip(lstrip(str));
}

#endif // UTILS_HPP
