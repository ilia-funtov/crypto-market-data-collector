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
	class kraken_trades_subscriber
	{
	public:
		kraken_trades_subscriber(
			const std::string & symbol,
			const std::chrono::milliseconds & request_period,
			const market_data_common::trade_handler_t & trade_handler,
			const market_data_common::error_handler_t & error_handler) :
			_request_period(request_period),
			_symbol(symbol),
			_trade_handler(trade_handler),
			_error_handler(error_handler),
			_running(true)
		{
			assert(!symbol.empty());
			assert(_trade_handler);
			assert(_error_handler);

			_thread = std::thread([this] { thread_loop(); });
		}

		~kraken_trades_subscriber()
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
			using namespace kraken;

			std::uint64_t since = 0;

			while (_running)
			{
				try
				{
					const auto trades = _kapi.get_trades(_symbol, since);
					if (since == 0)
					{
						since = trades.last_id;
						continue; // skip trades before present timestamp
					}

					for (const auto & record : trades.records)
					{
						if (record.order != order_type::market || record.deal == deal_type::unknown)
							continue;

						const auto side = (record.deal == deal_type::buy) ? market_data_common::taker_deal_type::buy : market_data_common::taker_deal_type::sell;
						_trade_handler(_symbol, record.price, record.volume, record.timestamp, side);
					}

					since = trades.last_id;

					{
						std::unique_lock<std::mutex> lock(_mtx);
						_thread_var.wait_for(lock, _request_period, [this] { return !_running; });
					}
				}
				catch (const std::exception & exc)
				{
					if (_error_handler)
						_error_handler(exc);
				}
			}
		}

		kraken_trades_subscriber(const kraken_trades_subscriber &) = delete;
		kraken_trades_subscriber& operator = (const kraken_trades_subscriber &) = delete;

		const std::chrono::milliseconds _request_period;
		const std::string _symbol;

		const market_data_common::trade_handler_t _trade_handler;
		const market_data_common::error_handler_t _error_handler;

		KAPI _kapi;

		std::atomic_bool _running;
		std::mutex _mtx;
		std::condition_variable _thread_var;
		std::thread _thread;
	};
} // namespace kraken