/*
Market data collector for crypto exchanges.
https://github.com/ilia-funtov/crypto-market-data-collector

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
Copyright (c) 2022 Ilia Funtov.
*/

#pragma once

#include <ctime>
#include <string>
#include <vector>

#include <boost/format.hpp>

#include <openssl/buffer.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/bio.h>

namespace bitmex
{
	namespace details
	{
		inline std::string hex_encode(const std::vector<unsigned char>& data)
		{
			boost::format fmt("%02x");

			std::string output;
			for (const auto b : data)
			{
				output += (fmt % static_cast<unsigned int>(b)).str();
			}

			return output;
		}

		template <typename data_t, typename _keyt>
		std::vector<unsigned char> hmac_sha256(const data_t & data, const _keyt & key)
		{
			unsigned int len = EVP_MAX_MD_SIZE;
			std::vector<unsigned char> digest(len);

			HMAC_CTX * ctx = HMAC_CTX_new();

			HMAC_Init_ex(ctx, std::data(key), (int)std::size(key), EVP_sha256(), nullptr);
			HMAC_Update(ctx, reinterpret_cast<const unsigned char *>(std::data(data)), std::size(data) * sizeof(typename data_t::value_type));
			HMAC_Final(ctx, std::data(digest), &len);

			HMAC_CTX_free(ctx);

			digest.resize(len);

			return digest;
		}
	}

	constexpr time_t time_to_expire = 10; // in seconds

	inline std::time_t get_expiration_time()
	{
		return std::time(nullptr) + time_to_expire;
	}

	inline std::string signature(const std::string& message, const std::string& secret)
	{
		using namespace details;
		return hex_encode(hmac_sha256(message, secret));
	}
}