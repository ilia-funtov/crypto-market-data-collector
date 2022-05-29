/*
Market data collector for crypto exchanges.
https://github.com/ilia-funtov/crypto-market-data-collector

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
Copyright (c) 2022 Ilia Funtov.
*/

#pragma once

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <stdexcept>
#include <string>

#if (defined(_WIN32) || defined(__WIN32__) || defined(WIN32))
#define _PLATFORM_WINDOWS_
#endif

namespace timestamp_parser
{
	namespace details
    {
        inline std::pair<std::uint64_t, std::uint64_t> parse_iso_timestamp(const std::string & iso_time)
        {
            unsigned int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, fractional = 0;

    #ifdef _PLATFORM_WINDOWS_
            const auto result = sscanf_s(iso_time.c_str(), "%u-%u-%uT%u:%u:%u.%uZ", &year, &month, &day, &hour, &minute, &second, &fractional);
    #else
            const auto result = sscanf(iso_time.c_str(), "%u-%u-%uT%u:%u:%u.%uZ", &year, &month, &day, &hour, &minute, &second, &fractional);
    #endif // _PLATFORM_WINDOWS_

            if (result < 6)
                throw std::runtime_error("Could not parse ISO time string.");

            struct tm timeinfo;
            timeinfo.tm_year = year - 1900;
            timeinfo.tm_mon = month - 1;
            timeinfo.tm_mday = day;
            timeinfo.tm_hour = hour;
            timeinfo.tm_min = minute;
            timeinfo.tm_sec = second;

    #ifdef _PLATFORM_WINDOWS_
            const auto timestamp_sec = _mkgmtime(&timeinfo);
    #else
            const auto timestamp_sec = timegm(&timeinfo);
    #endif // _PLATFORM_WINDOWS_

            if (timestamp_sec < 0)
                throw std::runtime_error("Could not make unix timestamp.");

            return std::make_pair(timestamp_sec, fractional);
        }
    };

    inline std::uint64_t parse_iso_timestamp_with_milliseconds(const std::string & iso_time)
	{
		const auto timestamp_pair = details::parse_iso_timestamp(iso_time);
		return (timestamp_pair.first * 1000 + timestamp_pair.second) * 1000;
	}

    inline std::uint64_t parse_iso_timestamp_with_microseconds(const std::string & iso_time)
    {
		const auto timestamp_pair = details::parse_iso_timestamp(iso_time);
        return timestamp_pair.first * 1000000 + timestamp_pair.second;
    }    
}