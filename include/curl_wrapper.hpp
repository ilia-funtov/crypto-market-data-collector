/*
Market data collector for crypto exchanges.
https://github.com/ilia-funtov/crypto-market-data-collector

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
Copyright (c) 2022 Ilia Funtov.
*/

#pragma once

#include <curl/curl.h>

#include <cassert>
#include <cstdlib>
#include <map>
#include <mutex>
#include <stdexcept>
#include <sstream>
#include <string>

#include <boost/algorithm/string.hpp>
#include <case_insensitive_string.hpp>

namespace curl
{
	namespace details
	{
		inline void initialize_global()
		{
			CURLcode code = curl_global_init(CURL_GLOBAL_ALL);
			if (code != CURLE_OK)
			{
				std::ostringstream oss;
				oss << "curl_global_init() failed: " << curl_easy_strerror(code);
				throw std::runtime_error(oss.str());
			}

			if (std::atexit(curl_global_cleanup) != 0)
			{
				curl_global_cleanup();
				throw std::runtime_error("Failed to register curl_global_cleanup at exit.");
			}
		}
	}

	class curl_error : public std::runtime_error
	{
	public:
		curl_error(CURLcode code, const char * message) :
			runtime_error((message != nullptr) ? std::string(message) : std::string("CURL error: " + std::to_string(code))),
			code_(code)
		{
		}

		CURLcode code() const { return code_; }
	private:
		CURLcode code_;
	};

	class curl_wrapper
	{
	public:
		using header_fields_t = std::map<ci_string, std::string>;

		curl_wrapper()
		{
			std::call_once(global_init_, [] { details::initialize_global(); });

			curl_ = curl_easy_init();
			if (curl_ == nullptr)
			{
				throw std::runtime_error("can't create curl handle");
			}

			setopt(CURLOPT_WRITEFUNCTION, curl_wrapper::write_cb);
		}

		curl_wrapper(const curl_wrapper&) = delete;
		curl_wrapper& operator=(const curl_wrapper&) = delete;
		curl_wrapper(curl_wrapper&&) = delete;
		curl_wrapper& operator=(curl_wrapper&&) = delete;

		~curl_wrapper()
		{
			if (curl_)
			{
				curl_easy_cleanup(curl_);
				curl_ = nullptr;
			}
		}

		template<typename ...Args>
		void setopt(CURLoption option, Args... args)
		{
			assert(curl_ != nullptr);

			check_result_code(curl_easy_setopt(curl_, option, args...));
		}

		std::string perform()
		{
			assert(curl_ != nullptr);

			std::string response;
			setopt(CURLOPT_WRITEDATA, static_cast<void*>(&response));

			check_result_code(curl_easy_perform(curl_));

			return response;
		}

		std::string perform_header_in(const std::vector<std::string> & strings)
		{
			assert(curl_ != nullptr);

			curl_slist* chunk = nullptr;
			for (const auto & str : strings)
			{
				chunk = curl_slist_append(chunk, str.c_str());
			}
			
			setopt(CURLOPT_HTTPHEADER, chunk);

			std::string response;
			setopt(CURLOPT_WRITEDATA, static_cast<void*>(&response));

			const auto code = curl_easy_perform(curl_);
			
			if (chunk != nullptr)
			{
				curl_slist_free_all(chunk);
			}

			check_result_code(code);

			return response;
		}
		
		std::pair<std::string, header_fields_t> perform_header_out()
		{
			assert(curl_ != nullptr);

			std::string response;
			setopt(CURLOPT_WRITEDATA, static_cast<void*>(&response));

			header_lines_t lines;
			setopt(CURLOPT_HEADERFUNCTION, header_callback); 
			setopt(CURLOPT_HEADERDATA, static_cast<void*>(&lines));

			const auto result_code = curl_easy_perform(curl_);

			setopt(CURLOPT_HEADERFUNCTION, nullptr);
			setopt(CURLOPT_HEADERDATA, nullptr);

			check_result_code(result_code);

			return std::make_pair(response, parse_fields(lines));
		}

		std::pair<std::string, header_fields_t> perform_header_in_header_out(const std::vector<std::string> & strings)
		{
			assert(curl_ != nullptr);

			curl_slist* chunk = nullptr;
			for (const auto & str : strings)
			{
				chunk = curl_slist_append(chunk, str.c_str());
			}

			setopt(CURLOPT_HTTPHEADER, chunk);

			std::string response;
			setopt(CURLOPT_WRITEDATA, static_cast<void*>(&response));

			header_lines_t lines;
			setopt(CURLOPT_HEADERFUNCTION, header_callback);
			setopt(CURLOPT_HEADERDATA, static_cast<void*>(&lines));

			const auto code = curl_easy_perform(curl_);

			setopt(CURLOPT_HEADERFUNCTION, nullptr);
			setopt(CURLOPT_HEADERDATA, nullptr);

			if (chunk != nullptr)
			{
				curl_slist_free_all(chunk);
			}

			check_result_code(code);

			return std::make_pair(response, parse_fields(lines));
		}
	private:
		using header_lines_t = std::vector<std::string>;

		header_fields_t parse_fields(const header_lines_t & lines)
		{
			header_fields_t result;
			for (auto line : lines)
			{
				const auto delimiter_pos = line.find(':');
				if (delimiter_pos == std::string::npos)
					continue;

				auto key = line.substr(0, delimiter_pos);
				auto value = line.substr(delimiter_pos + 1);
				
				boost::trim(key);
				boost::trim(value);

				result.emplace(key.c_str(), value); //.c_str() to initialize ci_string
			}

			return result;
		}

		static std::size_t header_callback(
			char *buffer,
			std::size_t size,
			std::size_t nitems,
			void *userdata)
		{
			/* received header is nitems * size long in 'buffer' NOT ZERO TERMINATED */
			/* 'userdata' is set with CURLOPT_HEADERDATA */

			const auto lines_ptr = reinterpret_cast<header_lines_t*>(userdata);
			if (lines_ptr == nullptr)
				return 0;

			auto & lines = *lines_ptr;
			const std::size_t numbytes = size * nitems;
			lines.emplace_back(buffer, numbytes);

			return numbytes;
		}

		static std::size_t write_cb(
			char* ptr,
			std::size_t size,
			std::size_t nmemb,
			void* userdata)
		{
			std::string* response = reinterpret_cast<std::string*>(userdata);
			std::size_t real_size = size * nmemb;

			response->append(ptr, real_size);
			return real_size;
		}

		void check_result_code(CURLcode code)
		{
			if (code != CURLE_OK)
			{
				throw curl_error(code, curl_easy_strerror(code));
			}
		}

		CURL * curl_ = nullptr; // CURL handle
		inline static std::once_flag global_init_;
	};
}