/*
Market data collector for crypto exchanges.
https://github.com/ilia-funtov/crypto-market-data-collector

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
Copyright (c) 2022 Ilia Funtov.
*/

#pragma once

#include <cassert>
#include <sstream>
#include <string>
#include <type_traits>
#include <boost/optional.hpp>

#include <nlohmann/json.hpp>

namespace json_helpers
{
	using json = nlohmann::json;

	inline double get_double(const json & object)
	{
		return object.is_number() ? object.get<double>() : std::stod(object.get<std::string>());
	};

	inline unsigned long get_ulong(const json & object)
	{
		return object.is_number() ? object.get<unsigned long>() : std::stoul(object.get<std::string>());
	};

	template <typename T>
	T get_value(const json & object, const char * property_name, T default_value = T())
	{		
		assert(property_name != nullptr);
		const auto iter = object.find(property_name);
		if (iter == object.end() || iter->is_null())
		{
			return default_value;
		}

		if constexpr (std::is_arithmetic_v<T>)
		{
			if (iter->is_string())
			{
				std::istringstream ss(iter->get<std::string>());

				T value;
				ss >> value;
				return value;
			}
		}

		return iter->get<T>();
	}

	template <typename T>
	T get_value(const json & object, const std::string & property_name, T default_value = T())
	{
		return get_value(object, property_name.c_str(), default_value);
	}

	template <typename T>
	void read_value(T & destination, const json & object, const char * property_name, T default_value = T())
	{
		destination = get_value<T>(object, property_name, default_value);
	}

	template <typename T>
	T get_required_value(const json & object, const char * property_name)
	{
		assert(property_name != nullptr);
		const auto iter = object.find(property_name);
		if (iter == object.end())
		{
			throw std::runtime_error(std::string("Could not find property ") + property_name);
		}

		if (iter->is_null())
		{
			throw std::runtime_error(std::string("Property ") + property_name + " has null value");
		}

		if constexpr (std::is_arithmetic_v<T>)
		{
			if (iter->is_string())
			{
				std::istringstream ss(iter->get<std::string>());

				T value;
				ss >> value;
				return value;
			}
		}

		return iter->get<T>();
	}

	template <typename T>
	T get_required_value(const json & object, const std::string & property_name)
	{
		return get_required_value<T>(object, property_name.c_str());
	}

	template <typename T>
	boost::optional<T> get_optional_value(const json & object, const char * property_name)
	{
		assert(property_name != nullptr);
		const auto iter = object.find(property_name);
		if (iter == object.end() || iter->is_null())
		{
			return {};
		}

		if constexpr (std::is_arithmetic_v<T>)
		{
			if (iter->is_string())
			{
				std::istringstream ss(iter->get<std::string>());

				T value;
				ss >> value;
				return value;
			}
		}

		return iter->get<T>();
	}

	template <typename T>
	boost::optional<T> get_optional_value(const json & object, const std::string & property_name)
	{
		return get_optional_value<T>(object, property_name.c_str());
	}
} // namespace json_helpers