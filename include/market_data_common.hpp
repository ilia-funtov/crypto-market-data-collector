/*
Market data collector for crypto exchanges.
https://github.com/ilia-funtov/crypto-market-data-collector

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
Copyright (c) 2022 Ilia Funtov.
*/

#pragma once

#include <cassert>
#include <cstdint>
#include <functional>
#include <exception>
#include <map>
#include <string>

namespace market_data_common
{
	enum class taker_deal_type : unsigned int
	{
		buy,
		sell
	};

	using order_map_t = std::map<double, double>;

	using book_handler_t = std::function<void(
		const std::string & symbol,
		const order_map_t & asks,
		const order_map_t & bids)>;

	using trade_handler_t = std::function<void(
		const std::string & symbol,
		double price,
		double volume,
		std::uint64_t timestamp,
		taker_deal_type side)>;

	using error_handler_t = std::function<void(const std::exception &)>;

	class order_book_subscriber_base
	{
	public:
		order_book_subscriber_base(
			const std::string & symbol,
			const book_handler_t & book_handler) :
			_symbol(symbol),
			_book_handler(book_handler)
		{
			assert(!_symbol.empty());
			assert(_book_handler);
		}
	
	protected:
		virtual ~order_book_subscriber_base() {}

		bool handle_order_book_if_consistent() const
		{
			if (is_order_book_consistent())
			{
				handle_order_book();
				return true;
			}

			return false;
		}

		bool is_order_book_consistent() const
		{
			double best_bid = 0, best_ask = 0;

			if (!asks_price_volume_map.empty())
				best_ask = asks_price_volume_map.cbegin()->first;

			if (!bids_price_volume_map.empty())
				best_bid = bids_price_volume_map.crbegin()->first;

			if (best_bid <= 0 || best_ask <= 0 || best_bid > best_ask)
			{
				return false;
			}

			return true;
		}

		void handle_order_book() const
		{
			_book_handler(_symbol, asks_price_volume_map, bids_price_volume_map);
		}

		const std::string _symbol;

		order_map_t asks_price_volume_map;
		order_map_t bids_price_volume_map;

		const book_handler_t _book_handler;
	
	private:
		order_book_subscriber_base(const order_book_subscriber_base &) = delete;
		order_book_subscriber_base& operator = (const order_book_subscriber_base &) = delete;
	};
}