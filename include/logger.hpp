/*
Market data collector for crypto exchanges.
https://github.com/ilia-funtov/crypto-market-data-collector

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
Copyright (c) 2022 Ilia Funtov.
*/

#pragma once

#include <cassert>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

#include <boost/current_function.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>

#define LOG_FATAL(logger) BOOST_LOG_SEV(*logger, ::boost::log::trivial::fatal)
#define LOG_ERROR(logger) BOOST_LOG_SEV(*logger, ::boost::log::trivial::error)
#define LOG_WARNING(logger) BOOST_LOG_SEV(*logger, ::boost::log::trivial::warning)
#define LOG_INFO(logger) BOOST_LOG_SEV(*logger, ::boost::log::trivial::info)
#define LOG_DEBUG(logger) BOOST_LOG_SEV(*logger, ::boost::log::trivial::debug)
#define LOG_TRACE(logger) BOOST_LOG_SEV(*logger, ::boost::log::trivial::trace)

namespace logger
{
	enum class severity_level : unsigned int
	{
		trace,
		debug,
		info,
		warning,
		error,
		fatal
	};

	namespace details
	{
		namespace logging = boost::log;
		namespace src = boost::log::sources;
		namespace sinks = boost::log::sinks;
		namespace keywords = boost::log::keywords;

		template <typename logger_t>
		class function_log_helper
		{
		public:
			function_log_helper(logger_t logger, const char * func_name) : _logger(logger), _func_name(func_name)
			{
				LOG_TRACE(_logger) << "Enter " << _func_name;
			}
			~function_log_helper() noexcept
			{
				try
				{
					LOG_TRACE(_logger) << "Leave " << _func_name;
				}
				catch (...)
				{
					// suppress all exceptions
				}
			}
		private:
			logger_t _logger;
			const char * const _func_name;
		};

		using namespace logging::trivial;
		using severity_logger_t = src::severity_logger<logging::trivial::severity_level>;

		using logger_t = std::shared_ptr<severity_logger_t>;
	}

	inline auto init(
		const char * log_prefix,
		severity_level severity = severity_level::info,
		std::uint64_t rotation_size = 10 * 1024 * 1024,
		const char * format = "%TimeStamp% %ProcessID% %ThreadID% %Severity% %Message%")
	{
		using namespace details;

		assert(log_prefix != nullptr);
		assert(format != nullptr);

		static std::once_flag once;
		static logger_t logger;

		const auto init_func = [&]()
		{
			logging::register_simple_formatter_factory<logging::trivial::severity_level, char>("Severity");

			logging::add_file_log
			(
				keywords::file_name = std::string(log_prefix) + "_%N.log",
				keywords::rotation_size = rotation_size,
				keywords::format = format,
				keywords::auto_flush = true,
				keywords::open_mode = std::ios_base::app
			);

			auto logger_severity = logging::trivial::info;
			switch (severity)
			{
			case severity_level::trace:
				logger_severity = logging::trivial::trace;
				break;
			case severity_level::debug:
				logger_severity = logging::trivial::debug;
				break;
			case severity_level::info:
				logger_severity = logging::trivial::info;
				break;
			case severity_level::warning:
				logger_severity = logging::trivial::warning;
				break;
			case severity_level::error:
				logger_severity = logging::trivial::error;
				break;
			case severity_level::fatal:
				logger_severity = logging::trivial::fatal;
				break;
			}

			logging::core::get()->set_filter(logging::trivial::severity >= logger_severity);

			logging::add_common_attributes();

			logger = std::make_shared<severity_logger_t>();
		};

		std::call_once(once, init_func);

		return logger;
	}

	inline severity_level string_to_severity(const char * str)
	{
		assert(str != nullptr);

		const static std::map<std::string, severity_level> level_map =
		{
			{ "trace", severity_level::trace },
			{ "debug", severity_level::debug },
			{ "info", severity_level::info },
			{ "warning", severity_level::warning },
			{ "error", severity_level::error },
			{ "fatal", severity_level::fatal }
		};

		const auto iter = level_map.find(str);
		if (iter == level_map.cend())
		{
			throw std::runtime_error(std::string("Logger severity level is unknown: ") + str);
		}

		return iter->second;
	}

	inline severity_level string_to_severity(const std::string & str)
	{
		return string_to_severity(str.c_str());
	}
}

#define LOG_FUNCTION(logger_object) ::logger::details::function_log_helper<decltype(logger_object)> __function_log_helper__(logger_object, BOOST_CURRENT_FUNCTION);