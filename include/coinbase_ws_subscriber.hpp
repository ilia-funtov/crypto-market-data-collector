/*
Market data collector for crypto exchanges.
https://github.com/ilia-funtov/crypto-market-data-collector

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
Copyright (c) 2022 Ilia Funtov.
*/

#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <utility>

#include <ws_subscriber_base.hpp>

#include <nlohmann/json.hpp>
#include <json_helpers.hpp>

namespace coinbase
{
	class coinbase_ws_subscriber: public websocket_subscriber::websocket_subscriber_base
	{
	public:
		using json = nlohmann::json;

		using event_handler_t = std::function<void(const json &)>;

		constexpr static char default_api_address[] = "ws-feed.exchange.coinbase.com";
		constexpr static unsigned int default_port = 443;

		coinbase_ws_subscriber(
			error_handler_t error_handler,
			const std::string & api_address = default_api_address,
			unsigned int port = default_port) :
			websocket_subscriber_base(error_handler, api_address, port, "//")
		{
		}

		coinbase_ws_subscriber(const coinbase_ws_subscriber &) = delete;
		coinbase_ws_subscriber & operator =(const coinbase_ws_subscriber &) = delete;
		coinbase_ws_subscriber(coinbase_ws_subscriber &&) = delete;
		coinbase_ws_subscriber & operator =(coinbase_ws_subscriber &&) = delete;

		~coinbase_ws_subscriber()
		{
			stop();
		}

		void subscribe(
			const std::string & channel_name,
			const std::string & product_id,
			const std::vector<std::string> & events,
			event_handler_t event_handler)
		{
			assert(!channel_name.empty());
			assert(!product_id.empty());
			assert(!events.empty());
			assert(event_handler);

			std::lock_guard<std::mutex> lock(_subscribe_mtx);
			_subscriptions_requested.emplace(channel_product_key{ channel_name, product_id }, event_handler);

			for (const auto & event : events)
			{
				_event_to_channel_map.emplace(event, channel_name);
			}
		}
	protected:
		void init_received(bool) noexcept override
		{
			// do nothing
		}

		bool is_init_received() const noexcept override
		{
			return true;
		}
	private:
		struct channel_product_key
		{
			std::string channel;
			std::string product_id;

			friend inline bool operator < (const channel_product_key & lhs, const channel_product_key & rhs)
			{
				if (lhs.channel < rhs.channel)
				{
					return true;
				}
				else if (lhs.channel == rhs.channel)
				{
					return lhs.product_id < rhs.product_id;
				}

				return false;
			}
		};

		void read_handler(const std::string & str) override
		{
			using namespace nlohmann;

			json object = json::parse(str);

			std::string event_type;
			std::string product_id;

			if (object.is_object())
			{
				json_helpers::read_value(event_type, object, "type");
				json_helpers::read_value(product_id, object, "product_id");
			}

			if (!event_type.empty() && !product_id.empty())
			{
				event_handler_t event_handler;

				{
					std::lock_guard<std::mutex> lock(_subscribe_mtx);
					const auto iter_event = _event_to_channel_map.find(event_type);
					if (iter_event != _event_to_channel_map.end())
					{
						const auto iter_handler = _subscriptions_requested.find({ iter_event->second , product_id });
						if (iter_handler != _subscriptions_requested.end())
						{
							event_handler = iter_handler->second;
						}
					}
				}

				if (event_handler)
					event_handler(object);
			}
			else if (event_type == "subscriptions")
			{
				register_subscription(object);
			}
		}

		void register_subscription(const json & object)
		{
			std::vector<json> channels;
			json_helpers::read_value(channels, object, "channels");

			if (channels.empty())
				return;

			std::lock_guard<std::mutex> lock(_subscribe_mtx);
			for (const auto & channel : channels)
			{
				std::string name;
				json_helpers::read_value(name, channel, "name");

				if (name.empty())
					continue;

				std::vector<std::string> products;
				json_helpers::read_value(products, channel, "product_ids");

				for (const auto & product : products)
				{
					if (product.empty())
						continue;

					_active_channels.insert({ name , product });
				}
			}
		}

		void subscribe_events() override
		{
			std::multimap<std::string, std::string> to_subscribe;

			{
				std::lock_guard<std::mutex> lock(_subscribe_mtx);

				for (const auto & sr : _subscriptions_requested)
				{
					if (_active_channels.find(sr.first) != _active_channels.end())
						continue;

					to_subscribe.emplace(sr.first.channel, sr.first.product_id);
				}
			}

			using namespace nlohmann;

			std::vector<json> channels;

			for (auto iter = to_subscribe.cbegin(); iter != to_subscribe.cend();)
			{
				const auto & channel = iter->first;
				const auto range = to_subscribe.equal_range(channel);

				std::set<std::string> products;
				for (auto iter_range = range.first; iter_range != range.second; ++iter_range)
				{
					products.insert(iter_range->second);
				}

				iter = range.second;

				json object_channel;
				object_channel["name"] = channel;
				object_channel["product_ids"] = products;

				channels.push_back(std::move(object_channel));
			}

			if (!channels.empty())
			{
				json object;
				object["type"] = "subscribe";
				object["channels"] = channels;

				auto subscribe_message = object.dump();
				websocket().write(subscribe_message);
			}
		}

		void reset_active_channels() override
		{
			std::lock_guard<std::mutex> lock(_subscribe_mtx);
			_active_channels.clear();
		}

		std::mutex _subscribe_mtx;
		std::map<channel_product_key, event_handler_t> _subscriptions_requested;
		std::map<std::string, std::string> _event_to_channel_map;
		std::set<channel_product_key> _active_channels;
	};
} // namespace coinbase