/*
Market data collector for crypto exchanges.
https://github.com/ilia-funtov/crypto-market-data-collector

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
Copyright (c) 2022 Ilia Funtov.
*/

#include <chrono>
#include <string>

#include <kraken_order_book_subscriber.hpp>
#include <kraken_trades_subscriber.hpp>

namespace kraken
{
	class kraken_market_data_subscriber
	{
	public:
		kraken_market_data_subscriber(
			const std::string & symbol,
            unsigned int order_book_size,
            const std::chrono::milliseconds & poll_period,
			const market_data_common::book_handler_t & book_handler,
			const market_data_common::trade_handler_t & trade_handler,
			const market_data_common::error_handler_t & error_handler):
            _order_book_subscriber(symbol, order_book_size, poll_period, book_handler, error_handler),
            _trades_subscriber(symbol, poll_period, trade_handler, error_handler)
		{
		}
    private:
        kraken_order_book_subscriber _order_book_subscriber;
        kraken_trades_subscriber _trades_subscriber;
    };
}