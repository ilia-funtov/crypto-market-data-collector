/*
Market data collector for crypto exchanges.
https://github.com/ilia-funtov/crypto-market-data-collector

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
Copyright (c) 2022 Ilia Funtov.

Permission is hereby  granted, free of charge, to any  person obtaining a copy
of this software and associated  documentation files (the "Software"), to deal
in the Software  without restriction, including without  limitation the rights
to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <iostream>
#include <fstream>
#include <map>
#include <string>
#include <thread>

#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>

#include <logger.hpp>
#include <market_data_provider.hpp>

#include <nlohmann/json.hpp>
#include <json_helpers.hpp>

market_data::general_symbol_description get_symbol_description(
	const std::string &symbol_config_file,
	const std::set<market_data::exchange_type> &exchanges,
	unsigned int depth)
{
	using namespace market_data;
	using namespace nlohmann;

	std::ifstream input(symbol_config_file);
	if (!input.is_open())
	{
		throw std::runtime_error("Could not open config file for symbol mapping");
	}

	json config;
	input >> config;

	general_symbol_description symbol_description;

	symbol_description.symbol_name = json_helpers::get_required_value<std::string>(config, "symbol");
	const std::map<std::string, std::string> symbol_mapping = config.at("mapping").get<std::map<std::string, std::string>>();

	for (const auto &mapping_item : symbol_mapping)
	{
		const auto exchange_type = market_data::get_exchange_type(mapping_item.first);
		if (exchanges.count(exchange_type))
		{
			source_symbol_description desc;
			desc.symbol_name = mapping_item.second;
			desc.order_book_size = depth;
			symbol_description.source_exchanges.emplace(exchange_type, desc);
		}
	}

	if (symbol_description.source_exchanges.empty())
	{
		throw std::runtime_error("Invalid configuration was provided for symbol mapping");
	}

	symbol_description.price_levels_num = depth;

	return symbol_description;
}

template <typename logger_t>
void run_loop(
	logger_t logger,
	const std::string &quote_dump_path,
	const std::string &symbol_config_file,
	const std::set<market_data::exchange_type> &exchanges,
	unsigned int duration_minutes,
	unsigned int blocks_num,
	unsigned int depth)
{
	using namespace market_data;

	const auto symbol_description = get_symbol_description(symbol_config_file, exchanges, depth);

	std::cout << "Collecting market data for symbol '" << symbol_description.symbol_name << "'" << std::endl;

	for (const auto &exchange_symbol : symbol_description.source_exchanges)
	{
		std::cout << market_data::get_exchange_name(exchange_symbol.first) << ": " << exchange_symbol.second.symbol_name << std::endl;
	}

	market_data_provider quote_provider(logger, symbol_description);
	quote_provider.set_dump_quotes(true, quote_dump_path, duration_minutes);

	std::this_thread::sleep_for(std::chrono::minutes(duration_minutes * blocks_num));
}

std::set<market_data::exchange_type> parse_exchanges(const std::string &str)
{
	std::vector<std::string> substrs;
	boost::split(substrs, str, boost::is_any_of(","));

	std::set<market_data::exchange_type> result;
	for (const auto &s : substrs)
	{
		result.insert(market_data::get_exchange_type(s));
	}
	return result;
}

int main(int argc, char *argv[])
{
	constexpr auto opt_help = "help";
	constexpr auto opt_exchanges = "exchanges";
	constexpr auto opt_dump_path = "dump-path";
	constexpr auto opt_symbol_config = "symbol-config";
	constexpr auto opt_duration = "duration";
	constexpr auto opt_blocks = "blocks";
	constexpr auto opt_depth = "depth";

	constexpr auto default_block_duration_in_minutes = 480; // 8 hours
	constexpr auto default_depth = 10;
	constexpr auto default_number_of_blocks = 1;

	try
	{
		namespace po = boost::program_options;
		po::options_description desc("Options");
		desc.add_options()
			(opt_help, "Print help message")
			(opt_exchanges, po::value<std::string>(), "Dump for selected exchanges only (bitfinex, bitmex, kraken, gdax)")
			(opt_dump_path, po::value<std::string>(), "Dump path for market data")
			(opt_symbol_config, po::value<std::string>(), "Config file for symbols name mapping")
			(opt_duration, po::value<unsigned int>()->default_value(default_block_duration_in_minutes), "Duration of one block in minutes")
			(opt_blocks, po::value<unsigned int>()->default_value(default_number_of_blocks), "Number of market data blocks")
			(opt_depth, po::value<unsigned int>()->default_value(default_depth), "Depth of the order book");

		po::variables_map vm;
		po::store(po::parse_command_line(argc, argv, desc), vm);

		if (vm.count(opt_help))
		{
			std::cout << "market-data-collector:" << std::endl
					  << desc << std::endl;
			return 0;
		}

		auto logger = logger::init("market-data-collector", logger::severity_level::info);

		try
		{
			if (vm.count(opt_dump_path) == 0)
			{
				throw std::runtime_error("Dump path is not defined");
			}

			if (vm.count(opt_symbol_config) == 0)
			{
				throw std::runtime_error("Config file for symbol mapping is not provided");
			}

			const auto duration = vm[opt_duration].as<unsigned int>();
			if (duration == 0)
			{
				throw std::runtime_error("Invalid duration");
			}

			const auto blocks_num = vm[opt_blocks].as<unsigned int>();
			if (blocks_num == 0)
			{
				throw std::runtime_error("Invalid number of blocks");
			}

			const auto depth = vm[opt_depth].as<unsigned int>();
			if (depth == 0)
			{
				throw std::runtime_error("Invalid order book depth");
			}

			const auto exchanges = (vm.count(opt_exchanges) != 0) ? parse_exchanges(vm[opt_exchanges].as<std::string>()) : market_data::get_supported_exchanges();
			if (exchanges.empty())
			{
				throw std::runtime_error("An empty list of exchanges was passed");
			}

			const auto dump_path = vm[opt_dump_path].as<std::string>();
			const auto symbol_config_file = vm[opt_symbol_config].as<std::string>();

			std::cout << "Dump market data to: " << dump_path << std::endl;
			std::cout << "Symbol config file: " << symbol_config_file << std::endl;
			std::cout << "Duration of one block: " << duration << " minute(s)" << std::endl;
			std::cout << "Number of market data blocks: " << blocks_num << std::endl;
			std::cout << "Depth of the order book: " << depth << std::endl;
			std::cout << "Exchanges:" << std::endl;
			for (const auto &ex : exchanges)
			{
				std::cout << market_data::get_exchange_name(ex) << std::endl;
			}

			std::cout << "Press Ctrl+C to stop." << std::endl;

			run_loop(logger, dump_path, symbol_config_file, exchanges, duration, blocks_num, depth);
		}
		catch (const std::exception &exc)
		{
			LOG_ERROR(logger) << exc.what();
			throw;
		}
	}
	catch (const std::exception &exc)
	{
		std::cerr << exc.what() << std::endl;
		return 1;
	}

	return 0;
}
