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

#ifndef BITMEX_API_PUBLIC_ONLY
#include <bitmex_authentication.hpp>
#endif // BITMEX_API_PUBLIC_ONLY

namespace bitmex
{
	class bitmex_ws_subscriber: public websocket_subscriber::websocket_subscriber_base
	{
	public:
		using json = nlohmann::json;

		using event_handler_t = std::function<void(const json &)>;

		constexpr static char default_api_address[] = "ws.bitmex.com";
		constexpr static char target[] = "/realtime";
		constexpr static unsigned int default_port = 443;

		bitmex_ws_subscriber(
			error_handler_t error_handler,
			const std::string & api_address = default_api_address,
			unsigned int port = default_port) :
			bitmex_ws_subscriber(error_handler, std::string(), std::string(), api_address, port)
		{
		}

		bitmex_ws_subscriber(
			error_handler_t error_handler,
			const std::string & key,
			const std::string & secret,
			const std::string & api_address = default_api_address,
			unsigned int port = default_port) :
			websocket_subscriber_base(error_handler, api_address, port, target),
			_key(key),
			_secret(secret)
		{
		}

		bitmex_ws_subscriber(const bitmex_ws_subscriber &) = delete;
		bitmex_ws_subscriber & operator =(const bitmex_ws_subscriber &) = delete;
		bitmex_ws_subscriber(bitmex_ws_subscriber &&) = delete;
		bitmex_ws_subscriber & operator =(bitmex_ws_subscriber &&) = delete;

		~bitmex_ws_subscriber()
		{
			stop();
		}

		void subscribe(
			const std::string & channel_name,
			const std::string & symbol,
			event_handler_t event_handler)
		{
			std::lock_guard<std::mutex> lock(_subscribe_mtx);
			_subscriptions_requested.emplace(channel_name, subscribe_info{ symbol , event_handler });
		}

		void unsubscribe(const std::string & channel_name)
		{
			std::lock_guard<std::mutex> lock(_subscribe_mtx);
			auto iter = _subscriptions_requested.find(channel_name);
			if (iter != _subscriptions_requested.end())
			{
				_subscriptions_requested.erase(iter);
				_to_unsubscribe.insert(channel_name);
			}
		}

	private:
		struct subscribe_info
		{
			std::string symbol;
			event_handler_t event_handler;
		};

		void read_handler(const std::string & str) override
		{
			using namespace nlohmann;

			json object = json::parse(str);

			if (is_init_received())
			{
				const auto iter_table = object.find("table");
				if (iter_table != object.end())
				{
					const auto channel_name = iter_table->get<std::string>();

					std::lock_guard<std::mutex> lock(_subscribe_mtx);
					if (_active_channels.find(channel_name) == _active_channels.end())
						return;

					const auto iter_subscription = _subscriptions_requested.find(channel_name);
					if (iter_subscription != _subscriptions_requested.end())
						iter_subscription->second.event_handler(object);
				}
				else if (object.find("subscribe") != object.end() && object.find("success") != object.end())
				{
					if (object["success"].get<bool>())
						register_subscription(object["subscribe"].get<std::string>());
				}
			}
			else if (object.find("info") != object.end())
			{
				init_received();
			}
		}

		void register_subscription(const std::string & subscription_name)
		{
			const auto index = subscription_name.find(':');
			const auto channel_name = subscription_name.substr(0, index);

			std::lock_guard<std::mutex> lock(_subscribe_mtx);
			_active_channels.emplace(channel_name);
		}

		void unregister_subscription(const std::string & subscription_name)
		{
			const auto index = subscription_name.find(':');
			const auto channel_name = subscription_name.substr(0, index);

			std::lock_guard<std::mutex> lock(_subscribe_mtx);
			_active_channels.erase(channel_name);
		}

		void subscribe_events() override
		{
			std::vector<std::pair<std::string, subscribe_info>> to_subscribe;

			{
				std::lock_guard<std::mutex> lock(_subscribe_mtx);

				for (const auto & sr : _subscriptions_requested)
				{
					const auto & name = sr.first;

					if (_active_channels.find(name) != _active_channels.end())
						continue;

					to_subscribe.emplace_back(name, sr.second);
				}
			}

			for (const auto & s : to_subscribe)
			{
				subscribe_event(s.first, s.second);
			}
		}

		void subscribe_event(const std::string & channel, const subscribe_info & info)
		{
			using namespace nlohmann;

			json object;
			object["op"] = "subscribe";
			object["args"] = json::array({ channel + ':' + info.symbol });

			auto subscribe_message = object.dump();
			websocket().write(subscribe_message);
		}

		void unsubscribe_events()
		{
			std::set<std::string> channels;

			{
				std::lock_guard<std::mutex> lock(_subscribe_mtx);

				for (const auto & name : _to_unsubscribe)
				{
					const auto iter = _active_channels.find(name);
					if (iter != _active_channels.end())
					{
						channels.insert(*iter);
					}
				}

				_to_unsubscribe.clear();
			}

			for (const auto & channel : channels)
			{
				unsubscribe_channel(channel);
			}
		}

		void unsubscribe_channel(const std::string & channel)
		{
			using namespace nlohmann;

			json object;
			object["op"] = "unsubscribe";
			object["args"] = json::array({ channel });

			auto subscribe_message = object.dump();
			websocket().write(subscribe_message);
		}

		void reset_active_channels() override
		{
			std::lock_guard<std::mutex> lock(_subscribe_mtx);
			_active_channels.clear();
		}

#ifndef BITMEX_API_PUBLIC_ONLY
		void authenticate() override
		{
			if (_key.empty() || _secret.empty())
				return;

			using namespace nlohmann;

			json object;
			object["op"] = "authKeyExpires";

			const auto expiration_time = get_expiration_time();
			const auto message = std::string("GET") + target + std::to_string(expiration_time);
			object["args"] = json::array({ _key, expiration_time, signature(message, _secret) });

			auto authenticate_message = object.dump();
			websocket().write(authenticate_message);
		}
#endif // BITMEX_API_PUBLIC_ONLY

		const std::string _key;
		const std::string _secret;

		std::mutex _subscribe_mtx;
		std::map<std::string, subscribe_info> _subscriptions_requested;
		std::set<std::string> _active_channels;
		std::set<std::string> _to_unsubscribe;
	};
}