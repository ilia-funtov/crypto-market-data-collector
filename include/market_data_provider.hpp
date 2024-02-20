/*
Market data collector for crypto exchanges.
https://github.com/ilia-funtov/crypto-market-data-collector

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
Copyright (c) 2022 Ilia Funtov.
*/

#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iterator>
#include <iomanip>
#include <string>
#include <thread>
#include <memory>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <queue>

#include <market_data_common.hpp>
#include <coinbase_market_data_subscriber.hpp>
#include <bitfinex_market_data_subscriber.hpp>
#include <kraken_market_data_subscriber.hpp>
#include <bitmex_market_data_subscriber.hpp>

namespace market_data
{
	namespace details
	{
		template <typename T, auto fn>
		struct deleter
		{
			void operator()(T *ptr)
			{
				fn(ptr);
			}
		};
	}

	enum class exchange_type : unsigned int
	{
		bitfinex,
		coinbase,
		kraken,
		bitmex
	};

	inline const char * get_exchange_name(exchange_type exchange)
	{
		switch (exchange)
		{
		case exchange_type::bitfinex:
			return "bitfinex";
		case exchange_type::coinbase:
			return "coinbase";
		case exchange_type::kraken:
			return "kraken";
		case exchange_type::bitmex:
			return "bitmex";
		}

		assert(false);
		return "";
	}

	inline std::set<exchange_type> get_supported_exchanges()
	{
		return std::set<exchange_type>({ exchange_type::bitfinex, exchange_type::coinbase, exchange_type::kraken, exchange_type::bitmex });
	}

	inline exchange_type get_exchange_type(const std::string & str)
	{
		std::string name;
		std::back_insert_iterator<decltype(name)> back_it_name(name);
		std::transform(str.cbegin(), str.cend(), back_it_name, [](char c) { return static_cast<char>(::tolower(c)); });
	
		static std::map<std::string, exchange_type> name_exchange = {
			{ "bitfinex", exchange_type::bitfinex },
			{ "coinbase", exchange_type::coinbase },
			{ "bitmex", exchange_type::bitmex },
			{ "kraken", exchange_type::kraken }
		};

		const auto iter = name_exchange.find(name);
		if (iter == name_exchange.cend())
			throw std::runtime_error("Unsupported exchange: " + str);

		return iter->second;
	}

	struct source_symbol_description
	{
		std::string symbol_name; // like BTC-USD
		unsigned int order_book_size;
	};

	struct general_symbol_description
	{
		std::string symbol_name; // like BTCUSD
		std::map<exchange_type, source_symbol_description> source_exchanges;
		unsigned int price_levels_num;
	};

	struct market_data_subscriber
	{
		std::function<void(
			exchange_type,
			const std::string &,
			const market_data_common::order_map_t &,
			const market_data_common::order_map_t &,
			std::uint64_t)> order_book_subscriber;

		std::function<void(
			exchange_type,
			const std::string &,
			double,
			double,
			std::uint64_t,
			market_data_common::taker_deal_type)> trade_subscriber;
	};

	template <typename logger_t>
	class market_data_provider
	{
	public:
		market_data_provider(
			logger_t logger,
			const general_symbol_description & symbol_description,
			const market_data_subscriber & subscriber = market_data_subscriber{}) :
			_symbol_description(symbol_description),
			_subscriber(subscriber),
			_logger(logger)
		{			
			LOG_INFO(_logger) << "Adding market data feeds for symbol: " << symbol_description.symbol_name;

			const auto & exchanges = _symbol_description.source_exchanges;
			for (auto iter = exchanges.cbegin(); iter != exchanges.cend(); ++iter)
			{
				switch (iter->first)
				{
				case exchange_type::coinbase:
					_coinbase_subscriber = std::make_unique<coinbase::coinbase_market_data_subscriber>(
						iter->second.symbol_name,
						[this](const auto & ...args) { order_book_handler(exchange_type::coinbase, args...); },
						[this](const auto & ...args) { trade_handler(exchange_type::coinbase, args...); },
						[this](const auto & ...args) { error_handler(exchange_type::coinbase, args...); });
					break;
				case exchange_type::bitfinex:
					_bitfinex_subscriber = std::make_unique<bitfinex::bitfinex_market_data_subscriber>(
						iter->second.symbol_name,
						iter->second.order_book_size,
						[this](const auto & ...args) { order_book_handler(exchange_type::bitfinex, args...); },
						[this](const auto & ...args) { trade_handler(exchange_type::bitfinex, args...); },
						[this](const auto & ...args) { error_handler(exchange_type::bitfinex, args...); });
					break;
				case exchange_type::kraken:
					_kraken_subscriber = std::make_unique<kraken::kraken_market_data_subscriber>(
						iter->second.symbol_name,
						iter->second.order_book_size,
						std::chrono::milliseconds(1000),
						[this](const auto & ...args) { order_book_handler(exchange_type::kraken, args...); },
						[this](const auto & ...args) { trade_handler(exchange_type::kraken, args...); },
						[this](const auto & ...args) { error_handler(exchange_type::kraken, args...); });
					break;
				case exchange_type::bitmex:
					_bitmex_subscriber = std::make_unique<bitmex::bitmex_market_data_subscriber>(
						iter->second.symbol_name,
						[this](const auto & ...args) { order_book_handler(exchange_type::bitmex, args...); },
						[this](const auto & ...args) { trade_handler(exchange_type::bitmex, args...); },
						[this](const auto & ...args) { error_handler(exchange_type::bitmex, args...); });
					break;
				}

				LOG_INFO(_logger) << get_exchange_name(iter->first) << " added as a market data feed: source symbol=" << iter->second.symbol_name << ", depth=" << iter->second.order_book_size;
			}
		}

		~market_data_provider()
		{
			{
				std::lock_guard<std::mutex> lock(_trades_dump_queue_mtx);
				_stop_dumping = true;
				_trades_dump_queue_var.notify_all(); // TODO: replace with notify_one
			}

			{
				std::lock_guard<std::mutex> lock(_prices_dump_queue_mtx);
				_prices_dump_queue_var.notify_all(); // TODO: replace with notify_one
			}

			if (_trades_dump_queue_thread.joinable())
				_trades_dump_queue_thread.join();

			if (_prices_dump_queue_thread.joinable())
				_prices_dump_queue_thread.join();
		}

		void set_dump_quotes(bool enabled, const std::string & path, unsigned int block_duration)
		{			
			assert(block_duration != 0);

			if (enabled && path.empty())
			{
				throw std::invalid_argument("Dump path is not defined.");
			}

			LOG_INFO(_logger) << "Configuration for market data dumping: enabled=" << enabled << ", path=" << path << ", block duration(minutes)=" << block_duration;

			_dump_path = path;
			_dump_quotes = enabled;
			_block_duration = std::chrono::minutes(block_duration);

			if (enabled)
			{
				_dump_start = std::chrono::system_clock::now();

				if (!_trades_dump_queue_thread.joinable())
				{
					_trades_dump_queue_thread = std::thread([this] { trades_dump_loop(); });
				}

				if (!_prices_dump_queue_thread.joinable())
				{
					_prices_dump_queue_thread = std::thread([this] { prices_dump_loop(); });
				}
			}
			else
			{
				_dump_start = std::chrono::system_clock::time_point();
			}
		}

	private:
		market_data_provider(const market_data_provider &) = delete;
		market_data_provider& operator = (const market_data_provider &) = delete;

		enum class deal_type : unsigned int
		{
			buy,
			sell
		};

		using timestamp_type = std::int64_t;
		struct trade_dump_record
		{
			exchange_type exchange;
			double price;
			double volume;
			timestamp_type timestamp;
			market_data_common::taker_deal_type side;
		};

		struct price_dump_record
		{
			exchange_type exchange;
			timestamp_type timestamp;
			std::vector<std::pair<double, double>> prices;
		};

		using file_handle = std::unique_ptr<FILE, details::deleter<FILE, fclose>>;

		void trades_dump_loop()
		{
			file_handle file;
			
			try
			{
				namespace fs = std::filesystem;
				fs::path path(_dump_path);				
				path /= "trades";
				if (!fs::exists(path))
				{
					fs::create_directories(path);
				}

				unsigned int block_index = 0;

				while (!_stop_dumping)
				{
					trade_dump_record trade_record;

					{
						std::unique_lock<std::mutex> lock(_trades_dump_queue_mtx);
						_trades_dump_queue_var.wait(
							lock,
							[this] { return _stop_dumping.load() || !_dump_queue_trades.empty(); });

						if (_stop_dumping)
							break;

						if (_dump_queue_trades.empty())
							continue;

						trade_record = _dump_queue_trades.front();
						_dump_queue_trades.pop();
					}

					const auto record_block_index = get_block_index(trade_record.timestamp);
					if (file == nullptr || record_block_index != block_index)
					{
						file.reset();

						const auto file_path = path / (_symbol_description.symbol_name + '_' + std::to_string(record_block_index) + ".csv");

						std::string str_path = file_path.string();
						file.reset(fopen(str_path.c_str(), "at"));
						if (file != nullptr)
						{
							setbuf(file.get(), nullptr);
							block_index = record_block_index;
						}
					}

					if (file != nullptr)
					{
						std::ostringstream ss;
						ss << std::fixed;
						ss << get_exchange_name(trade_record.exchange) << ',' <<
							std::setprecision(2) << trade_record.price << ',' << std::setprecision(8) <<
							((trade_record.side == market_data_common::taker_deal_type::buy) ? trade_record.volume : -trade_record.volume) << ',' << trade_record.timestamp << "\n";

						const auto & str = ss.str();
						if (fwrite(str.data(), 1, str.size(), file.get()) != str.size())
						{
							LOG_ERROR(_logger) << "File writing error for trades";
						}
					}
				}			
			}
			catch (const std::exception & exc)
			{
				LOG_ERROR(_logger) << "Trades dump loop error: " << exc.what();
			}
		}

		void prices_dump_loop()
		{
			file_handle file;

			try
			{
				namespace fs = std::filesystem;
				fs::path path(_dump_path);
				path /= "prices";
				if (!fs::exists(path))
				{
					fs::create_directories(path);
				}

				unsigned int block_index = 0;

				while (!_stop_dumping)
				{
					price_dump_record price_record;

					{
						std::unique_lock<std::mutex> lock(_prices_dump_queue_mtx);
						_prices_dump_queue_var.wait(
							lock,
							[this] { return _stop_dumping.load() || !_dump_queue_prices.empty(); });

						if (_stop_dumping)
							break;

						if (_dump_queue_prices.empty())
							continue;

						price_record = _dump_queue_prices.front();
						_dump_queue_prices.pop();
					}

					const auto record_block_index = get_block_index(price_record.timestamp);
					if (file == nullptr || record_block_index != block_index)
					{
						file.reset();

						const auto file_path = path / (_symbol_description.symbol_name + '_' + std::to_string(record_block_index) + ".csv");

						std::string str_path = file_path.string();
						file.reset(fopen(str_path.c_str(), "at"));
						if (file != nullptr)
						{
							setbuf(file.get(), nullptr);
							block_index = record_block_index;
						}
					}

					if (file != nullptr)
					{
						std::ostringstream ss;
						ss << std::fixed;
						ss << get_exchange_name(price_record.exchange) << ',' << price_record.timestamp;
						for (const auto & price_pair : price_record.prices)
						{
							ss << ',' << std::setprecision(2) << price_pair.first << ',' << std::setprecision(8) << price_pair.second;
						}
						ss << '\n';

						const auto & str = ss.str();
						if (fwrite(str.data(), 1, str.size(), file.get()) != str.size())
						{
							LOG_ERROR(_logger) << "File writing error for prices";
						}
					}
				}
			}
			catch (const std::exception & exc)
			{
				LOG_ERROR(_logger) << "Prices dump loop error: " << exc.what();
			}
		}

		unsigned int get_block_index(timestamp_type timestamp) const
		{			
			const auto dump_start_mcs = std::chrono::duration_cast<std::chrono::microseconds>(_dump_start.time_since_epoch()).count();
			return (timestamp > dump_start_mcs && _block_duration.count() != 0) ? ((timestamp - dump_start_mcs) / std::chrono::duration_cast<std::chrono::microseconds>(_block_duration).count()) : 0;
		}

		void error_handler(exchange_type exchange, const std::exception & exc)
		{
			LOG_ERROR(_logger) << get_exchange_name(exchange) << ": " << exc.what();
		};

		void order_book_handler(
			exchange_type exchange,
			const std::string & symbol,
			const market_data_common::order_map_t & asks,
			const market_data_common::order_map_t & bids)
		{
			const auto timestamp = std::chrono::system_clock::now();
			const auto timestamp_mcs = std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch()).count();
			if (_subscriber.order_book_subscriber)
			{
				_subscriber.order_book_subscriber(
					exchange,
					symbol,
					asks,
					bids,
					timestamp_mcs);
			}

			if (_dump_quotes)
			{
				std::vector<std::pair<double, double>> order_book_prices;
				order_book_prices.reserve(_symbol_description.price_levels_num * 2);
				auto iter_bid = bids.crbegin();
				auto iter_ask = asks.cbegin();
				for (unsigned int n = 0;
					n != _symbol_description.price_levels_num && iter_bid != bids.crend() && iter_ask != asks.cend();
					++n, ++iter_bid, ++iter_ask)
				{
					order_book_prices.emplace_back(iter_bid->first, iter_bid->second);
					order_book_prices.emplace_back(iter_ask->first, iter_ask->second);
				}

				price_dump_record record;
				record.exchange = exchange;
				record.prices = std::move(order_book_prices);
				record.timestamp = timestamp_mcs;

				std::lock_guard<std::mutex> lock(_prices_dump_queue_mtx);
				_dump_queue_prices.push(record);
				_prices_dump_queue_var.notify_one();
			}
		}

		void trade_handler(
			exchange_type exchange,
			const std::string & symbol,
			double price,
			double volume,
			timestamp_type timestamp,
			market_data_common::taker_deal_type side)
		{
			if (_subscriber.trade_subscriber)
			{
				_subscriber.trade_subscriber(
					exchange,
					symbol,
					price,
					volume,
					timestamp,
					side);
			}

			if (_dump_quotes)
			{
				trade_dump_record record = 
				{
					exchange,
					price,
					volume,
					timestamp,
					side
				};

				std::lock_guard<std::mutex> lock(_trades_dump_queue_mtx);
				_dump_queue_trades.push(record);
				_trades_dump_queue_var.notify_one();
			}
		}

		const general_symbol_description _symbol_description;
		const market_data_subscriber _subscriber;

		logger_t _logger;

		std::string _dump_path;
		std::chrono::minutes _block_duration;
		std::chrono::system_clock::time_point _dump_start;
		std::atomic_bool _dump_quotes{false};
		std::atomic_bool _stop_dumping{false};
		
		std::queue<trade_dump_record> _dump_queue_trades;
		std::mutex _trades_dump_queue_mtx;
		std::condition_variable _trades_dump_queue_var;

		std::queue<price_dump_record> _dump_queue_prices;
		std::mutex _prices_dump_queue_mtx;
		std::condition_variable _prices_dump_queue_var;

		std::unique_ptr<coinbase::coinbase_market_data_subscriber> _coinbase_subscriber;
		std::unique_ptr<bitfinex::bitfinex_market_data_subscriber> _bitfinex_subscriber;
		std::unique_ptr<kraken::kraken_market_data_subscriber> _kraken_subscriber;
		std::unique_ptr<bitmex::bitmex_market_data_subscriber> _bitmex_subscriber;

		std::thread _trades_dump_queue_thread;
		std::thread _prices_dump_queue_thread;
	};
}