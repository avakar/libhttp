#include "utils.hpp"

std::vector<std::string> split(std::string_view str, char sep)
{
	std::vector<std::string> r;

	char const * first = str.data();
	char const * last = first + str.size();

	for (char const * cur = first; cur != last; ++cur)
	{
		if (*cur != sep)
			continue;

		r.emplace_back(first, cur - first);
		first = cur + 1;
	}

	if (first != last)
		r.emplace_back(first, last - first);

	return r;
}

bool starts_with(std::string_view str, std::string_view prefix)
{
	return str.size() >= prefix.size() && memcmp(str.data(), prefix.data(), prefix.size()) == 0;
}
