/*
Market data collector for crypto exchanges.
https://github.com/ilia-funtov/crypto-market-data-collector

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
Copyright (c) 2022 Ilia Funtov.
*/

#pragma once

#include <cassert>
#include <cmath>
#include <map>
#include <vector>
#include <string>

#include <bitfinex_ws_subscriber.hpp>
#include <json_helpers.hpp>
#include <market_data_common.hpp>

namespace bitfinex
{
	class bitfinex_market_data_subscriber: public market_data_common::order_book_subscriber_base
	{
	public:
		bitfinex_market_data_subscriber(
			const std::string & symbol,
			unsigned int depth,
			const market_data_common::book_handler_t & book_handler,
			const market_data_common::trade_handler_t & trade_handler,
			const market_data_common::error_handler_t & error_handler,
			const std::string & api_address = bitfinex_ws_subscriber::default_api_address,
			unsigned int port = bitfinex_ws_subscriber::default_port) :
			order_book_subscriber_base(symbol, book_handler),
			_trade_handler(trade_handler),
			_ws_subscriber(error_handler, api_address, port)
		{
			assert(depth != 0);
			assert(trade_handler);

			{
				const auto len = (depth <= 25) ? 25 : 100;
				const std::map<std::string, std::string> params =
				{
					{ "symbol" , symbol },
					{ "prec" , "P0" },
					{ "freq" , "F0" },
					{ "len" , std::to_string(len) }
				};

				_ws_subscriber.subscribe(
					"book",
					params,
					[this](const nlohmann::json & object) { order_book_event_handler(object); });
			}

			{
				const std::map<std::string, std::string> params =
				{
					{ "symbol" , symbol }
				};

				_ws_subscriber.subscribe(
					"trades",
					params,
					[this](const nlohmann::json & object) { trades_event_handler(object); });
			}
		}

		bitfinex_market_data_subscriber(const bitfinex_market_data_subscriber &) = delete;
		bitfinex_market_data_subscriber& operator = (const bitfinex_market_data_subscriber &) = delete;
		bitfinex_market_data_subscriber(bitfinex_market_data_subscriber &&) = delete;
		bitfinex_market_data_subscriber& operator = (bitfinex_market_data_subscriber &&) = delete;
	private:
		using json = nlohmann::json;

		void order_book_event_handler(const json & object)
		{
			if (!object.is_array())
				return;

			const auto & value = object.at(0);
			if (!value.is_array())
				return;

			const auto & items = value.get<std::vector<json>>();
			if (items.empty())
				return;

			const auto parse = [this](const auto & items) -> bool
			{
				if (items.size() != 3)
					return false;

				if (items[0].is_number() && items[1].is_number() && items[2].is_number())
				{
					const auto price = items[0].template get<double>();
					const auto count = items[1].template get<unsigned int>();
					const auto amount = items[2].template get<double>();

					if (count > 0)
					{
						if (amount > 0)
						{
							bids_price_volume_map[price] = amount;
						}
						else if (amount < 0)
						{
							asks_price_volume_map[price] = -amount;
						}
					}
					else if (count == 0)
					{
						if (amount == 1)
						{
							bids_price_volume_map.erase(price);
						}
						else if (amount == -1)
						{
							asks_price_volume_map.erase(price);
						}
					}

					return true;
				}

				return false;
			};

			if (!parse(items))
			{
				asks_price_volume_map.clear();
				bids_price_volume_map.clear();

				for (const auto & item : items)
				{
					parse(item);
				}
			}

			if (!handle_order_book_if_consistent())
			{
				_ws_subscriber.restart();
			}
		}

		void trades_event_handler(const json & object)
		{
			if (!object.is_array())
				return;

			const auto & message_type = object.at(0);
			if (!message_type.is_string() || message_type != "te")
				return;

			const auto & message_content = object.at(1);
			if (!message_content.is_array() || message_content.size() < 4)
				return;
			
			const auto timestamp = message_content[1].get<std::uint64_t>() * 1000; // convert to microsec
			const auto amount = message_content[2].get<double>();
			const auto price = message_content[3].get<double>();
			const auto side = (amount < 0) ? market_data_common::taker_deal_type::sell : market_data_common::taker_deal_type::buy;

			_trade_handler(_symbol, price, fabs(amount), timestamp, side);
		}
		
		const market_data_common::trade_handler_t _trade_handler;
		bitfinex_ws_subscriber _ws_subscriber;
	};
}