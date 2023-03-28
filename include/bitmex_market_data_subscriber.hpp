/*
Market data collector for crypto exchanges.
https://github.com/ilia-funtov/crypto-market-data-collector

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
Copyright (c) 2022 Ilia Funtov.
*/

#pragma once

#include <cassert>
#include <exception>
#include <string>
#include <vector>

#include <bitmex_ws_subscriber.hpp>
#include <json_helpers.hpp>
#include <market_data_common.hpp>
#include <timestamp_parser.hpp>

namespace bitmex
{
	class bitmex_market_data_subscriber: public market_data_common::order_book_subscriber_base
	{
	public:
		bitmex_market_data_subscriber(
			const std::string & symbol,
			const market_data_common::book_handler_t & book_handler,
			const market_data_common::trade_handler_t & trade_handler,
			const market_data_common::error_handler_t & error_handler,
			const std::string & api_address = bitmex_ws_subscriber::default_api_address,
			unsigned int port = bitmex_ws_subscriber::default_port) :
			order_book_subscriber_base(symbol, book_handler),
			_trade_handler(trade_handler),
			_symbol(symbol),
			_ws_subscriber(error_handler, api_address, port)
		{
			assert(_trade_handler);

			_ws_subscriber.subscribe(
				"orderBook10",
				symbol,
				[this](const json & object) { level2_top10_event_handler(object); });

			_ws_subscriber.subscribe(
				"trade",
				symbol,
				[this](const json & object) { trades_event_handler(object); });
		}
	private:
		using json = nlohmann::json;

		void level2_top10_event_handler(const json & object)
		{
			if (!object.is_object())
				return;

			const auto iter_action = object.find("action");
			if (iter_action == object.end())
				return;

			const auto & action = iter_action->get<std::string>();
			if (action != "update")
				return;

			const auto parse_book_records = [](const auto & book_records, auto & book_map)
			{
				for (const auto & book_record : book_records)
				{
					if (!book_record.is_array())
						continue;

					const std::vector<json> & book_record_array = book_record.template get<std::vector<json>>();
					if (book_record_array.size() != 2)
						continue;

					const auto price = book_record_array[0].get<double>();
					const auto size = book_record_array[1].get<double>();

					if (price != 0)
						book_map.emplace(price, size / price);
				}
			};

			asks_price_volume_map.clear();
			bids_price_volume_map.clear();

			const std::vector<json> & data = object["data"];
			for (const auto & record : data)
			{
				const auto & symbol = record["symbol"];
				if (symbol != _symbol)
					continue;

				parse_book_records(record["asks"], asks_price_volume_map);
				parse_book_records(record["bids"], bids_price_volume_map);
			}
			
			if (!handle_order_book_if_consistent())
			{
				_ws_subscriber.restart();
			}
		}

		void trades_event_handler(const json & object)
		{
			if (!object.is_object())
				return;

			const auto iter_action = object.find("action");
			if (iter_action == object.end())
				return;

			const auto & action = iter_action->get<std::string>();
			if (action == "insert")
			{
				const std::vector<json> & data = object["data"];
				for (const auto & record : data)
				{
					const auto & symbol = record["symbol"];
					if (symbol != _symbol)
						continue;

					const auto & side = record["side"].get<std::string>();
					if (side.empty())
						continue;

					const auto volume = record["homeNotional"].get<double>();
					const auto price = record["price"].get<double>();
					if (volume <= 0 || price <= 0)
						continue;

					const auto & timestamp_str = record["timestamp"].get<std::string>();
					if (timestamp_str.empty())
						continue;

					const auto timestamp = timestamp_parser::parse_iso_timestamp_with_milliseconds(timestamp_str);

					const auto side_char = side.at(0);
					if (side_char == 'S' || side_char == 's')
					{
						_trade_handler(_symbol, price, volume, timestamp, market_data_common::taker_deal_type::sell);
					}
					else if (side_char == 'B' || side_char == 'b')
					{
						_trade_handler(_symbol, price, volume, timestamp, market_data_common::taker_deal_type::buy);
					}
				}
			}
		}

		const market_data_common::trade_handler_t _trade_handler;
		const std::string _symbol;

		bitmex_ws_subscriber _ws_subscriber;

		bitmex_market_data_subscriber(const bitmex_market_data_subscriber &) = delete;
		bitmex_market_data_subscriber& operator = (const bitmex_market_data_subscriber &) = delete;
	};
} // namespace bitmex