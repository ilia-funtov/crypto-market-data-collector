/*
Market data collector for crypto exchanges.
https://github.com/ilia-funtov/crypto-market-data-collector

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
Copyright (c) 2022 Ilia Funtov.
*/

#pragma once

#include <cassert>
#include <cstdio>
#include <ctime>
#include <exception>
#include <string>
#include <vector>

#include <coinbase_ws_subscriber.hpp>
#include <json_helpers.hpp>
#include <market_data_common.hpp>
#include <timestamp_parser.hpp>

namespace coinbase
{
	class coinbase_market_data_subscriber: public market_data_common::order_book_subscriber_base
	{
	public:
		coinbase_market_data_subscriber(
			const std::string & symbol,
			const market_data_common::book_handler_t & book_handler,
			const market_data_common::trade_handler_t & trade_handler,
			const market_data_common::error_handler_t & error_handler,
			const std::string & api_address = coinbase_ws_subscriber::default_api_address,
			unsigned int port = coinbase_ws_subscriber::default_port) :
			order_book_subscriber_base(symbol, book_handler),
			_trade_handler(trade_handler),
			_ws_subscriber(error_handler, api_address, port)
		{
			assert(_trade_handler);

			_ws_subscriber.subscribe(
				"level2_batch",
				symbol,
				{ "snapshot", "l2update" },
				[this](const json & object) { level2_event_handler(object); });

			_ws_subscriber.subscribe(
				"matches",
				symbol,
				{ "match" },
				[this](const json & object) { matches_event_handler(object); });
		}

		coinbase_market_data_subscriber(const coinbase_market_data_subscriber &) = delete;
		coinbase_market_data_subscriber& operator = (const coinbase_market_data_subscriber &) = delete;
		coinbase_market_data_subscriber(coinbase_market_data_subscriber &&) = delete;
		coinbase_market_data_subscriber& operator = (coinbase_market_data_subscriber &&) = delete;
	private:
		using json = nlohmann::json;

		void level2_event_handler(const json & object)
		{
			if (!object.is_object())
				return;

			const auto & product_id = object["product_id"];
			if (product_id != _symbol)
			{
				_ws_subscriber.restart();
				return;
			}

			const auto & type = object["type"];
			if (type == "snapshot")
			{
				asks_price_volume_map.clear();
				bids_price_volume_map.clear();

				const auto parse_orders = [](const auto & object, const auto & name, auto & dest_map)
				{
					const std::vector<json> & orders = object[name];
					for (const auto & order : orders)
					{
						if (order.is_array() && order.size() >= 2)
						{
							const auto price = json_helpers::get_double(order[0]);
							const auto volume = json_helpers::get_double(order[1]);
							if (price >= 0 && volume >= 0)
								dest_map.emplace(price, volume);
						}
					}
				};

				parse_orders(object, "bids", bids_price_volume_map);
				parse_orders(object, "asks", asks_price_volume_map);
			}
			else if (type == "l2update")
			{
				const std::vector<json> & changes = object["changes"];
				for (const auto & change : changes)
				{
					if (change.is_array() && change.size() >= 3 && change[0].is_string())
					{
						const auto & side = change[0].get<std::string>();
						const double price = json_helpers::get_double(change[1]);
						const double volume = json_helpers::get_double(change[2]);

						market_data_common::order_map_t * map = nullptr;

						if (side == "buy")
						{
							map = &bids_price_volume_map;
						}
						else if (side == "sell")
						{
							map = &asks_price_volume_map;
						}

						if (map != nullptr && price >= 0)
						{
							if (volume <= 0)
							{
								map->erase(price);
							}
							else
							{
								(*map)[price] = volume;
							}
						}
					}
				}
			}

			if (!handle_order_book_if_consistent())
			{
				_ws_subscriber.restart();
			}
		}

		void matches_event_handler(const json & object)
		{
			if (!object.is_object())
				return;

			const auto & product_id = object["product_id"];
			if (product_id != _symbol)
			{
				_ws_subscriber.restart();
				return;
			}

			const auto & side = object["side"].get<std::string>();
			market_data_common::taker_deal_type deal;
			if (side == "buy")
				deal = market_data_common::taker_deal_type::sell;
			else if (side == "sell")
				deal = market_data_common::taker_deal_type::buy;
			else
				throw std::runtime_error("Could not parse deal type");

			const auto & iso_time = object["time"].get<std::string>();
			const double price = json_helpers::get_double(object["price"]);
			const double volume = json_helpers::get_double(object["size"]);
			const auto timestamp = timestamp_parser::parse_iso_timestamp_with_microseconds(iso_time);

			_trade_handler(_symbol, price, volume, timestamp, deal);
		}

		const market_data_common::trade_handler_t _trade_handler;

		coinbase_ws_subscriber _ws_subscriber;
	};
} // namespace coinbase