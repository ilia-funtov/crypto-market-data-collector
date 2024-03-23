/*
Market data collector for crypto exchanges.
https://github.com/ilia-funtov/crypto-market-data-collector

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
Copyright (c) 2022 Ilia Funtov.
*/

#pragma once

#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <utility>

#include <ws_subscriber_base.hpp>

#include <nlohmann/json.hpp>
#include <json_helpers.hpp>

namespace bitfinex
{
	class bitfinex_ws_subscriber: public websocket_subscriber::websocket_subscriber_base
	{
	public:
		using json = nlohmann::json;

		using event_handler_t = std::function<void(const json &)>;

		constexpr static char default_api_address[] = "api-pub.bitfinex.com";
		constexpr static unsigned int default_port = 443;

		bitfinex_ws_subscriber(
			error_handler_t error_handler,
			const std::string & api_address = default_api_address,
			unsigned int port = default_port) :
			websocket_subscriber_base(error_handler, api_address, port, "/ws/" + std::to_string(required_api_version))
		{
		}

		bitfinex_ws_subscriber(const bitfinex_ws_subscriber &) = delete;
		bitfinex_ws_subscriber & operator =(const bitfinex_ws_subscriber &) = delete;
		bitfinex_ws_subscriber(bitfinex_ws_subscriber &&) = delete;
		bitfinex_ws_subscriber & operator =(bitfinex_ws_subscriber &&) = delete;

		~bitfinex_ws_subscriber()
		{
			stop();
		}

		void subscribe(
			const std::string & channel_name,
			const std::map<std::string, std::string> & params,
			event_handler_t event_handler)
		{
			std::lock_guard<std::mutex> lock(_subscribe_mtx);
			_subscriptions_requested.emplace(channel_name, subscribe_info{ params , event_handler });
		}

		void unsubscribe(const std::string & channel_name)
		{
			std::lock_guard<std::mutex> lock(_subscribe_mtx);
			auto iter = _subscriptions_requested.find(channel_name);
			if (iter != _subscriptions_requested.end())
			{
				_subscriptions_requested.erase(iter);
				return;
			}

			_to_unsubscribe.insert(channel_name);
		}

	private:
		struct subscribe_info
		{
			std::map<std::string, std::string> params;
			event_handler_t event_handler;
		};

		void read_handler(const std::string & str) override
		{
			using namespace nlohmann;

			json object = json::parse(str);

			std::string event_name;
			if (object.is_object())
			{
				json_helpers::read_value(event_name, object, "event");
			}

			if (is_init_received())
			{
				if (object.is_array() && object.size() >= 2)
				{
					const auto channel_id = object.at(0).get<unsigned int>();
						
					json data_object;
					for (unsigned int n = 1; n != object.size(); ++n)
					{
						data_object.push_back(object.at(n));
					}

					event_handler_t event_handler;

					{
						std::lock_guard<std::mutex> lock(_subscribe_mtx);
						const auto iter_name = _channel_id_name_map.find(channel_id);
						if (iter_name != _channel_id_name_map.end())
						{
							const auto iter_handler = _subscriptions_requested.find(iter_name->second);
							if (iter_handler != _subscriptions_requested.end())
							{
								event_handler = iter_handler->second.event_handler;
							}
						}
					}

					if (event_handler)
						event_handler(data_object);
				}
				else if (event_name == "subscribed")
				{
					register_subscription(object);
				}
				else if (event_name == "unsubscribed")
				{
					unregister_subscription(object);
				}
			}
			else
			{
				if (event_name == "info")
				{
					unsigned int version = 0;
					json_helpers::read_value(version, object, "version");
					if (version == required_api_version)
					{
						init_received();
					}
					else
					{
						throw std::runtime_error("Unexpected version of bitfinex websocket api.");
					}
				}
			}
		}

		void register_subscription(const json & object)
		{
			std::string channel;
			json_helpers::read_value(channel, object, "channel");

			unsigned int channel_id = 0;
			json_helpers::read_value(channel_id, object, "chanId");

			if (!channel.empty() && channel_id != 0)
			{
				std::lock_guard<std::mutex> lock(_subscribe_mtx);

				_channel_id_name_map.emplace(channel_id, channel);
				_active_channels.emplace(channel, channel_id);
			}
		}

		void unregister_subscription(const json & object)
		{
			std::string status;
			json_helpers::read_value(status, object, "status");

			unsigned int channel_id = 0;
			json_helpers::read_value(channel_id, object, "chanId");

			if (status == "OK" && channel_id != 0)
			{
				std::lock_guard<std::mutex> lock(_subscribe_mtx);

				const auto iter = _channel_id_name_map.find(channel_id);
				if (iter != _channel_id_name_map.end())
				{
					_active_channels.erase(iter->second);
					_channel_id_name_map.erase(iter);
				}
			}
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
			object["event"] = "subscribe";
			object["channel"] = channel;

			for (const auto & param : info.params)
			{
				object[param.first] = param.second;
			}

			auto subscribe_message = object.dump();
			websocket().write(subscribe_message);
		}

		void unsubscribe_events()
		{
			std::set<unsigned int> channels;

			{
				std::lock_guard<std::mutex> lock(_subscribe_mtx);

				for (const auto & name : _to_unsubscribe)
				{
					const auto iter = _active_channels.find(name);
					if (iter != _active_channels.end())
					{
						channels.insert(iter->second);
					}
				}

				_to_unsubscribe.clear();
			}

			for (const auto id : channels)
			{
				unsubscribe_channel(id);
			}
		}

		void unsubscribe_channel(unsigned int id)
		{
			using namespace nlohmann;

			json object;
			object["event"] = "unsubscribe";
			object["chanId"] = id;

			auto message = object.dump();
			websocket().write(message);
		}

		void reset_active_channels() override
		{
			std::lock_guard<std::mutex> lock(_subscribe_mtx);
			_channel_id_name_map.clear();
			_active_channels.clear();
		}

		static constexpr unsigned int required_api_version = 2;

		std::mutex _subscribe_mtx;
		std::map<std::string, subscribe_info> _subscriptions_requested;
		std::map<unsigned int, std::string> _channel_id_name_map;
		std::map<std::string, unsigned int> _active_channels;
		std::set<std::string> _to_unsubscribe;
	};
}