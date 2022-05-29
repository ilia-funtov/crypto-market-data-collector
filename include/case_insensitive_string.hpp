// Original work http://www.gotw.ca/gotw/029.htm

#pragma once

#include <string>
#include <iostream>
#include <cctype>

struct ci_char_traits : public std::char_traits<char>
{
	static char to_upper(char ch)
	{
		return std::toupper((unsigned char)ch);
	}

	static bool eq(char c1, char c2)
	{
		return to_upper(c1) == to_upper(c2);
	}

	static bool ne(char c1, char c2)
	{
		return to_upper(c1) != to_upper(c2);
	}

	static bool lt(char c1, char c2)
	{
		return to_upper(c1) < to_upper(c2);
	}

	static int compare(const char* s1, const char* s2, size_t n)
	{
		while (n-- != 0)
		{
			if (to_upper(*s1) < to_upper(*s2)) return -1;
			if (to_upper(*s1) > to_upper(*s2)) return 1;
			++s1; ++s2;
		}
		return 0;
	}

	static const char* find(const char* s, int n, char a)
	{
		auto const ua(to_upper(a));
		while (n-- != 0)
		{
			if (to_upper(*s) == ua)
				return s;
			s++;
		}
		return nullptr;
	}
};

using ci_string = std::basic_string<char, ci_char_traits>;

inline std::ostream& operator << (std::ostream& os, const ci_string& str)
{
	return os.write(str.data(), str.size());
}