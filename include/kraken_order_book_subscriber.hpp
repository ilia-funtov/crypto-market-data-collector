/*
Market data collector for crypto exchanges.
https://github.com/ilia-funtov/crypto-market-data-collector

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
Copyright (c) 2022 Ilia Funtov.
*/

#pragma once

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <string>
#include <thread>
#include <map>
#include <mutex>

#include <kraken_api.hpp>
#include <market_data_common.hpp>

namespace kraken
{
	class kraken_order_book_subscriber: public market_data_common::order_book_subscriber_base
	{
	public:
		kraken_order_book_subscriber(
			const std::string & symbol,
			unsigned int order_book_size,
			const std::chrono::milliseconds & quote_period,
			const market_data_common::book_handler_t & book_handler,
			const market_data_common::error_handler_t & error_handler) :
			order_book_subscriber_base(symbol, book_handler),
			_order_book_size(order_book_size),
			_quote_period(quote_period),
			_symbol(symbol),
			_error_handler(error_handler),
			_running(true)
		{
			assert(!symbol.empty());
			assert(_error_handler);

			_thread = std::thread([this] { thread_loop(); });
		}

		kraken_order_book_subscriber(const kraken_order_book_subscriber &) = delete;
		kraken_order_book_subscriber& operator = (const kraken_order_book_subscriber &) = delete;
		kraken_order_book_subscriber(kraken_order_book_subscriber &&) = delete;
		kraken_order_book_subscriber& operator = (kraken_order_book_subscriber &&) = delete;

		~kraken_order_book_subscriber()
		{
			{
				std::unique_lock<std::mutex> lock(_mtx);
				_running = false;
				_thread_var.notify_one();
			}

			if (_thread.joinable())
				_thread.join();
		}
	private:
		void thread_loop()
		{
			while (_running)
			{
				try
				{
					const auto & orders = _kapi.get_order_book(_symbol, _order_book_size);
					if (!orders.asks.empty() && !orders.bids.empty())
					{
						market_data_common::order_map_t bids;
						for (const auto & bid : orders.bids)
						{
							bids.emplace(bid.price, bid.volume);
						}

						market_data_common::order_map_t asks;
						for (const auto & ask : orders.asks)
						{
							asks.emplace(ask.price, ask.volume);
						}

						asks_price_volume_map = std::move(asks);
						bids_price_volume_map = std::move(bids);

						handle_order_book_if_consistent();
					}

					{
						std::unique_lock<std::mutex> lock(_mtx);
						_thread_var.wait_for(lock, _quote_period, [this] { return !_running; });
					}
				}
				catch (const std::exception & exc)
				{
					if (_error_handler)
						_error_handler(exc);
				}
			}
		}

		const unsigned int _order_book_size;
		const std::chrono::milliseconds _quote_period;
		const std::string _symbol;

		const market_data_common::error_handler_t _error_handler;

		KAPI _kapi;

		std::atomic_bool _running;
		std::mutex _mtx;
		std::condition_variable _thread_var;
		std::thread _thread;
	};
} // namespace kraken