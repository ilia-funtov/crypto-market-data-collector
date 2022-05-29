/*
Market data collector for crypto exchanges.
https://github.com/ilia-funtov/crypto-market-data-collector

Rework of https://github.com/voidloop/krakenapi

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
Copyright (c) 2022 Ilia Funtov.
*/

#pragma once

#include <cstring>
#include <ctime>
#include <cstdint>
#include <cctype>

#include <atomic>
#include <chrono>
#include <map>
#include <vector>
#include <regex>
#include <stdexcept>
#include <string>
#include <sstream>
#include <type_traits>

#ifndef KRAKEN_API_PUBLIC_ONLY
#include <openssl/buffer.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/bio.h>
#endif // KRAKEN_API_PUBLIC_ONLY

#include <nlohmann/json.hpp>
#include <json_helpers.hpp>
#include <curl_wrapper.hpp>

//------------------------------------------------------------------------------
namespace kraken
{
	using asset_class_type = std::string;
	using asset_type = std::string;
	using currency_type = double;
	using timestamp_type = std::uint64_t;
	using order_id_type = std::string;
	using leverage_type = unsigned int;
	using userref_type = std::int32_t;

	enum class order_type : unsigned int
	{
		unknown = 0,
		market,
		limit, //(price = limit price)
		stop_loss, //(price = stop loss price)
		take_profit, //(price = take profit price)
		stop_loss_profit, //(price = stop loss price, price2 = take profit price)
		stop_loss_profit_limit, //(price = stop loss price, price2 = take profit price)
		stop_loss_limit, //(price = stop loss trigger price, price2 = triggered limit price)
		take_profit_limit, //(price = take profit trigger price, price2 = triggered limit price)
		trailing_stop, //(price = trailing stop offset)
		trailing_stop_limit, //(price = trailing stop offset, price2 = triggered limit offset)
		stop_loss_and_limit, //(price = stop loss price, price2 = limit price)
		settle_position
	};

	enum class order_status_type : unsigned int
	{
		unknown = 0,
		pending, // order pending book entry
		open, // open order
		closed, //closed order
		canceled, // order canceled
		expired // order expired
	};

	enum class deal_type : unsigned int
	{
		unknown = 0,
		buy,
		sell
	};

	enum class order_flags : unsigned int
	{
		none = 0,
		viqc = 1, //volume in quote currency(not available for leveraged orders)
		fcib = 2, //prefer fee in base currency
		fciq = 4, //prefer fee in quote currency
		nompp = 8, //no market price protection
		post = 16 //post only order(available when ordertype = limit)
	};

	enum class misc_info : unsigned int
	{
		none = 0,
		stopped = 1, // triggered by stop price
		touched = 2, // triggered by touch price
		liquidated = 4, // liquidation
		partial = 8 // partial fill
	};

	struct order
	{
		order_id_type id;
		order_status_type status;
		timestamp_type opentm;
		timestamp_type starttm;
		timestamp_type expiretm;
		timestamp_type closetm;
		std::string reason;

		struct description
		{
			std::string pair;
			deal_type type;
			order_type ordertype;
			currency_type price;
			currency_type price2;
			leverage_type levegage;
			std::string order;
			std::string close;
		} desc;

		currency_type vol;
		currency_type vol_exec;
		currency_type cost;
		currency_type fee;
		currency_type price;
		currency_type stopprice;
		currency_type limitprice;
		misc_info misc;
		order_flags oflags;
	};

	struct trade_balance_info
	{
		currency_type equivalent_balance;
		currency_type trade_balance;
		currency_type margin;
		currency_type unrealized_net_profit_loss;
		currency_type cost_basis;
		currency_type floating;
		currency_type equity;
		currency_type free_margin;
		double margin_level;
	};

	struct new_order
	{
		std::string pair;
		deal_type deal;
		order_type type;
		currency_type price;
		currency_type price2;
		currency_type volume;
		leverage_type leverage;
		order_flags oflags;
		timestamp_type starttm;
		timestamp_type expiretm;
		userref_type userref;
	};

	struct trade_record
	{
		currency_type price;
		currency_type volume;
		timestamp_type timestamp;
		deal_type deal;
		order_type order;
		std::string misc;
	};

	using get_account_balance_response = std::map<asset_type, currency_type>;
	using get_open_orders_response = std::map<order_id_type, order>;

	struct get_closed_orders_response
	{
		std::map<order_id_type, order> orders;
		std::uint64_t count;
	};

	struct add_order_response
	{
		std::vector<order_id_type> orders;
		std::string order_description;
		std::string conditional_description;
	};

	struct cancel_order_response
	{
		std::uint64_t count;
		bool pending;
	};

	struct get_trades_response
	{
		std::vector<trade_record> records;
		std::uint64_t last_id;
	};

	struct order_book_record
	{
		currency_type price;
		currency_type volume;
		timestamp_type timestamp;
	};

	struct get_order_book_response
	{
		std::vector<order_book_record> asks;
		std::vector<order_book_record> bids;
	};

	class kraken_api_error : public std::runtime_error
	{
	public:
		inline kraken_api_error(const std::string & message) : std::runtime_error(message)
		{
		}
	};

	namespace details
	{
		constexpr auto KRAKEN_URL = "https://api.kraken.com";
		constexpr auto MARKET = "market";
		constexpr auto LIMIT = "limit";
		constexpr auto STOP_LOSS = "stop-loss";
		constexpr auto TAKE_PROFIT = "take-profit";
		constexpr auto STOP_LOSS_PROFIT = "stop-loss-profit";
		constexpr auto STOP_LOSS_PROFIT_LIMIT = "stop-loss-profit-limit";
		constexpr auto STOP_LOSS_LIMIT = "stop-loss-limit";
		constexpr auto TAKE_PROFIT_LIMIT = "take-profit-limit";
		constexpr auto TRAILING_STOP = "trailing-stop";
		constexpr auto TRAILING_STOP_LIMIT = "trailing-stop-limit";
		constexpr auto STOP_LOSS_AND_LIMIT = "stop-loss-and-limit";
		constexpr auto SETTLE_POSITION = "settle-position";

		constexpr auto PENDING = "pending";
		constexpr auto OPEN = "open";
		constexpr auto CLOSED = "closed";
		constexpr auto CANCELED = "canceled";
		constexpr auto EXPIRED = "expired";

		constexpr auto BUY = "buy";
		constexpr auto SELL = "sell";

		constexpr auto VIQC = "viqc";
		constexpr auto FCIB = "fcib";
		constexpr auto FCIQ = "fciq";
		constexpr auto NOMPP = "nompp";
		constexpr auto POST = "post";

		constexpr auto STOPPED = "stopped";
		constexpr auto TOUCHED = "touched";
		constexpr auto LIQUIDATED = "liquidated";
		constexpr auto PARTIAL = "partial";

		using json = nlohmann::json;
		using namespace json_helpers;

		inline timestamp_type make_timestamp(double in)
		{
			return (timestamp_type)(in * 1000);
		}

		inline order_type order_type_from_string(const std::string & str)
		{
			using namespace details;

			static const std::map<std::string, order_type> type_map =
			{
				{ MARKET, order_type::market },
				{ LIMIT, order_type::limit },
				{ STOP_LOSS, order_type::stop_loss },
				{ TAKE_PROFIT, order_type::take_profit },
				{ STOP_LOSS_PROFIT, order_type::stop_loss_profit },
				{ STOP_LOSS_PROFIT_LIMIT, order_type::stop_loss_profit_limit },
				{ STOP_LOSS_LIMIT, order_type::stop_loss_limit },
				{ TAKE_PROFIT_LIMIT, order_type::take_profit_limit },
				{ TRAILING_STOP, order_type::trailing_stop },
				{ TRAILING_STOP_LIMIT, order_type::trailing_stop_limit },
				{ STOP_LOSS_AND_LIMIT, order_type::stop_loss_and_limit },
				{ SETTLE_POSITION, order_type::settle_position }
			};

			const auto & iter = type_map.find(str);
			if (iter == type_map.cend())
			{
				return order_type::unknown;
			}

			return iter->second;
		}

		inline std::string order_type_to_string(order_type type)
		{
			using namespace details;

			static const std::map<order_type, std::string> type_map =
			{
				{ order_type::market, MARKET },
				{ order_type::limit, LIMIT },
				{ order_type::stop_loss, STOP_LOSS },
				{ order_type::take_profit, TAKE_PROFIT },
				{ order_type::stop_loss_profit, STOP_LOSS_PROFIT },
				{ order_type::stop_loss_profit_limit, STOP_LOSS_PROFIT_LIMIT },
				{ order_type::stop_loss_limit, STOP_LOSS_LIMIT },
				{ order_type::take_profit_limit, TAKE_PROFIT_LIMIT },
				{ order_type::trailing_stop, TRAILING_STOP },
				{ order_type::trailing_stop_limit, TRAILING_STOP_LIMIT },
				{ order_type::stop_loss_and_limit, STOP_LOSS_AND_LIMIT },
				{ order_type::settle_position, SETTLE_POSITION }
			};

			const auto & iter = type_map.find(type);
			if (iter == type_map.cend())
			{
				throw std::logic_error("Unknown value of order_type.");
			}

			return iter->second;
		}

		inline order_status_type order_status_from_string(const std::string & str)
		{
			using namespace details;

			static const std::map<std::string, order_status_type> type_map =
			{
				{ PENDING, order_status_type::pending },
				{ OPEN, order_status_type::open },
				{ CLOSED, order_status_type::closed },
				{ CANCELED, order_status_type::canceled },
				{ EXPIRED, order_status_type::expired }
			};

			const auto & iter = type_map.find(str);
			if (iter == type_map.cend())
			{
				return order_status_type::unknown;
			}

			return iter->second;
		}

		inline deal_type deal_type_from_string(const std::string & str)
		{
			using namespace details;

			if (str == BUY)
			{
				return deal_type::buy;
			}
			else if (str == SELL)
			{
				return deal_type::sell;
			}

			return deal_type::unknown;
		}

		inline std::string deal_type_to_string(deal_type type)
		{
			using namespace details;

			if (type == deal_type::buy)
			{
				return BUY;
			}
			else if (type == deal_type::sell)
			{
				return SELL;
			}

			throw std::logic_error("Unknown value of deal_type.");
		}

		inline order_flags order_flags_from_string(const std::string & str)
		{
			using namespace details;

			static const std::map<std::string, order_flags> type_map =
			{
				{ FCIB, order_flags::fcib },
				{ FCIQ, order_flags::fciq },
				{ NOMPP, order_flags::nompp },
				{ POST, order_flags::post },
				{ VIQC, order_flags::viqc }
			};

			order_flags flags = order_flags::none;

			std::regex re("\\w+");

			std::sregex_token_iterator begin(str.cbegin(), str.cend(), re);

			std::for_each(
				begin,
				std::sregex_token_iterator(),
				[&](const auto & value)
			{
				auto type_iter = type_map.find(value);
				if (type_iter != type_map.cend())
				{
					flags = static_cast<order_flags>(static_cast<unsigned int>(flags) |
						static_cast<unsigned int>(type_iter->second));
				}
			});

			return flags;
		}

		inline std::string order_flags_to_string(order_flags flags_in)
		{
			using namespace details;

			static const std::vector<std::pair<order_flags, std::string>> flags =
			{
				{ order_flags::fcib, FCIB },
				{ order_flags::fciq, FCIQ },
				{ order_flags::nompp, NOMPP },
				{ order_flags::post, POST },
				{ order_flags::viqc, VIQC }
			};

			std::string flags_out;

			const auto int_flag = static_cast<unsigned int>(flags_in);
			for (const auto & flag : flags)
			{
				if (static_cast<unsigned int>(flag.first) & int_flag)
				{
					if (!flags_out.empty()) flags_out += ',';
					flags_out += flag.second;
				}
			}

			return flags_out;
		}

		inline misc_info misc_info_from_string(const std::string & str)
		{
			using namespace details;

			static const std::map<std::string, misc_info> type_map =
			{
				{ LIQUIDATED, misc_info::liquidated },
				{ PARTIAL, misc_info::partial },
				{ STOPPED, misc_info::stopped },
				{ TOUCHED, misc_info::touched },
			};

			misc_info info = misc_info::none;

			std::regex re("\\w+");

			std::sregex_token_iterator begin(str.cbegin(), str.cend(), re);

			std::for_each(
				begin,
				std::sregex_token_iterator(),
				[&](const auto & value)
				{
					auto type_iter = type_map.find(value);
					if (type_iter != type_map.cend())
					{
						info = static_cast<misc_info>(static_cast<unsigned int>(info) |
							static_cast<unsigned int>(type_iter->second));
					}
				});

			return info;
		}

		inline std::string timestamp_to_string(timestamp_type tm)
		{
			return std::to_string(tm / 1000);
		}

		inline order parse_order(const json & in)
		{
			const auto & descr = in["descr"];

			order order{};

			order.status = order_status_from_string(get_value<std::string>(in, "status"));
			order.opentm = make_timestamp(get_value<double>(in, "opentm"));
			order.starttm = make_timestamp(get_value<double>(in, "starttm"));
			order.expiretm = make_timestamp(get_value<double>(in, "expiretm"));
			order.closetm = make_timestamp(get_value<double>(in, "closetm"));
			read_value(order.reason, in, "reason");

			read_value(order.desc.pair, descr, "pair");

			order.desc.type = deal_type_from_string(get_value<std::string>(descr, "type"));
			order.desc.ordertype = order_type_from_string(get_value<std::string>(descr, "ordertype"));

			read_value(order.desc.price, descr, "price");
			read_value(order.desc.price2, descr, "price2");

			{
				const auto leverage = get_value<std::string>(descr, "leverage");
				order.desc.levegage = (leverage == "none" || leverage.empty()) ? 0 : (unsigned int)std::stoul(leverage);
			}

			read_value(order.desc.order, descr, "order");
			read_value(order.desc.close, descr, "close");

			read_value(order.vol, in, "vol");
			read_value(order.vol_exec, in, "vol_exec");
			read_value(order.cost, in, "cost");
			read_value(order.fee, in, "fee");
			read_value(order.price, in, "price");
			read_value(order.stopprice, in, "stopprice");
			read_value(order.limitprice, in, "limitprice");
			order.misc = misc_info_from_string(get_value<std::string>(in, "misc"));
			order.oflags = order_flags_from_string(get_value<std::string>(in, "oflags"));

			return order;
		}

		inline json parse_response(const std::string & response)
		{
			const auto & object = json::parse(response);
			const auto & error_list = object["error"].get<std::vector<std::string>>();
			if (!error_list.empty())
			{
				std::string error_message;
				for (const auto & error : error_list)
				{
					if (!error.empty() && std::tolower(error[0]) == 'e')
					{
						if (!error_message.empty())
						{
							error_message += ", ";
						}

						error_message += error;
					}
				}
				if (!error_message.empty())
				{
					throw kraken_api_error(error_message);
				}
			}

			return object["result"];
		}

		inline std::vector<order_book_record> parse_order_book_records(const std::vector<json> & in)
		{
			std::vector<order_book_record> out;
			out.reserve(in.size());

			for (const auto & item : in)
			{
				auto num = item.size();
				auto iter = item.cbegin();

				order_book_record record{};
				if (num--) record.price = get_double(*iter++);
				if (num--) record.volume = get_double(*iter++);
				if (num--) record.timestamp = make_timestamp(get_double(*iter++));

				if (record.price > 0 && record.volume > 0)
				{
					out.push_back(record);
				}
			}

			return out;
		}

#ifndef KRAKEN_API_PUBLIC_ONLY
		inline std::vector<unsigned char> sha256(const std::string& data)
		{
			std::vector<unsigned char> digest(SHA256_DIGEST_LENGTH);

			SHA256_CTX ctx;
			SHA256_Init(&ctx);
			SHA256_Update(&ctx, data.c_str(), data.length());
			SHA256_Final(digest.data(), &ctx);

			return digest;
		}

		//------------------------------------------------------------------------------
		// helper function to decode a base64 string to a vector of bytes:
		inline std::vector<unsigned char> b64_decode(const std::string& data)
		{
			BIO* b64 = BIO_new(BIO_f_base64());
			BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

			BIO* bmem = BIO_new_mem_buf((void*)data.c_str(), (int)data.length());
			bmem = BIO_push(b64, bmem);

			std::vector<unsigned char> output(data.length());
			int decoded_size = BIO_read(bmem, output.data(), (int)output.size());
			BIO_free_all(bmem);

			if (decoded_size < 0)
			{
				throw std::runtime_error("failed while decoding base64.");
			}

			return output;
		}

		//------------------------------------------------------------------------------
		// helper function to encode a vector of bytes to a base64 string:
		inline std::string b64_encode(const std::vector<unsigned char>& data)
		{
			BIO* b64 = BIO_new(BIO_f_base64());
			BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

			BIO* bmem = BIO_new(BIO_s_mem());
			b64 = BIO_push(b64, bmem);

			BIO_write(b64, data.data(), (int)data.size());
			BIO_flush(b64);

			BUF_MEM* bptr = nullptr;
			BIO_get_mem_ptr(b64, &bptr);

			std::string output(bptr->data, bptr->length);
			BIO_free_all(b64);

			return output;
		}

		//------------------------------------------------------------------------------
		// helper function to hash with HMAC algorithm:
		inline std::vector<unsigned char>
			hmac_sha512(const std::vector<unsigned char>& data,
				const std::vector<unsigned char>& key)
		{
			unsigned int len = EVP_MAX_MD_SIZE;
			std::vector<unsigned char> digest(len);

			HMAC_CTX * ctx = HMAC_CTX_new();

			HMAC_Init_ex(ctx, key.data(), (int)key.size(), EVP_sha512(), nullptr);
			HMAC_Update(ctx, data.data(), data.size());
			HMAC_Final(ctx, digest.data(), &len);

			HMAC_CTX_free(ctx);

			return digest;
		}
#endif // KRAKEN_API_PUBLIC_ONLY
	} // namespace details

	class KAPI
	{
	public:
#ifndef KRAKEN_API_PUBLIC_ONLY 
		KAPI(const std::string& key, const std::string& secret,
			const std::string& url, const std::string& version) :
			key_(key), secret_(secret), url_(url), version_(version)

		{
			init();
		}

		KAPI(const std::string& key, const std::string& secret) :
			key_(key), secret_(secret), url_(details::KRAKEN_URL), version_("0")
		{
			init();
		}
#endif // KRAKEN_API_PUBLIC_ONLY

		KAPI() :
			key_(""), secret_(""), url_(details::KRAKEN_URL), version_("0")
		{
			init();
		}

		get_order_book_response get_order_book(const std::string & pair, std::uint64_t count = 0)
		{
			using namespace details;

			input_params input;
			input.emplace("pair", pair);
			if (count != 0) input.emplace("count", std::to_string(count));

			const auto & response = public_method("Depth", input);
			const auto & result = parse_response(response);
			
			const auto & pair_item = get_value<json>(result, pair);
			const auto & asks = get_value<std::vector<json>>(pair_item, "asks");
			const auto & bids = get_value<std::vector<json>>(pair_item, "bids");

			get_order_book_response out
			{
				parse_order_book_records(asks),
				parse_order_book_records(bids),
			};

			return out;
		}

		get_trades_response get_trades(const std::string & pair, std::uint64_t since = 0)
		{
			using namespace details;

			input_params input;
			input.emplace("pair", pair);
			if (since != 0) input.emplace("since", std::to_string(since));

			const auto & response = public_method("Trades", input);
			const auto & result = parse_response(response);

			get_trades_response out;
			const auto trades_list = get_value<std::vector<json>>(result, pair);
			out.records.reserve(trades_list.size());

			for (const auto & trade_item : trades_list)
			{
				auto num = trade_item.size();
				auto iter = trade_item.cbegin();

				trade_record record{};
				if (num--) record.price = get_double(*iter++);
				if (num--) record.volume = get_double(*iter++);
				if (num--) record.timestamp = make_timestamp(get_double(*iter++));

				if (num--)
				{
					const auto & str = iter++->get<std::string>();
					record.deal = (str == "b") ? deal_type::buy : ((str == "s") ? deal_type::sell : deal_type::unknown);
				}

				if (num--)
				{
					const auto & str = iter++->get<std::string>();
					record.order = (str == "m") ? order_type::market : ((str == "l") ? order_type::limit : order_type::unknown);
				}

				if (num--) record.misc = iter++->get<std::string>();

				if (record.price > 0 && record.volume > 0 && record.timestamp != 0 &&
					record.deal != deal_type::unknown && record.order != order_type::unknown)
				{
					out.records.push_back(record);
				}
			}

			read_value(out.last_id, result, "last");

			return out;
		}

#ifndef KRAKEN_API_PUBLIC_ONLY 
		get_account_balance_response get_account_balance()
		{
			using namespace details;

			const auto & response = private_method("Balance");
			const auto & result = parse_response(response);
			
			get_account_balance_response balance;
			for (json::const_iterator it = result.cbegin(); it != result.cend(); ++it)
			{
				balance.emplace(it.key(), std::stod(it.value().get<std::string>()));
			}

			return balance;
		}

		get_open_orders_response get_open_orders(bool include_trades = false, userref_type userref = 0)
		{
			using namespace details;

			input_params input;
			if (include_trades) input.emplace("trades", std::to_string(true));
			if (userref != 0) input.emplace("userref", std::to_string(userref));

			const auto & response = private_method("OpenOrders", input);
			const auto & result = parse_response(response);

			const auto & open_orders = result["open"];

			std::map<order_id_type, order> orders;

			for (json::const_iterator it = open_orders.cbegin(); it != open_orders.cend(); ++it)
			{
				auto order = parse_order(it.value());
				order.id = it.key();
				orders.emplace(it.key(), std::move(order));
			}

			return orders;
		}

		get_closed_orders_response get_closed_orders(bool include_trades = false, userref_type userref = 0)
		{
			using namespace details;

			input_params input;
			if (include_trades) input.emplace("trades", std::to_string(true));
			if (userref != 0) input.emplace("userref", std::to_string(userref));

			const auto & response = private_method("ClosedOrders", input);
			const auto & result = parse_response(response);

			const auto & closed_orders = result["closed"];

			std::map<order_id_type, order> orders;

			for (json::const_iterator it = closed_orders.cbegin(); it != closed_orders.cend(); ++it)
			{
				auto order = parse_order(it.value());
				order.id = it.key();
				orders.emplace(it.key(), std::move(order));
			}

			const auto count = get_value<std::uint64_t>(result, "count");

			return get_closed_orders_response{ std::move(orders), count };
		}

		trade_balance_info get_trade_balance(const asset_class_type & aclass = asset_class_type(), const asset_type & asset = asset_type())
		{
			using namespace details;

			input_params input;
			if (!aclass.empty()) input.emplace("aclass", aclass);
			if (!asset.empty()) input.emplace("asset", asset);

			const auto & response = private_method("TradeBalance", input);
			const auto & result = parse_response(response);

			trade_balance_info balance{};

			read_value(balance.equivalent_balance, result, "eb");
			read_value(balance.trade_balance, result, "tb");
			read_value(balance.margin, result, "m");
			read_value(balance.unrealized_net_profit_loss, result, "n");
			read_value(balance.cost_basis, result, "c");
			read_value(balance.floating, result, "v");
			read_value(balance.equity, result, "e");
			read_value(balance.free_margin, result, "mf");
			read_value(balance.margin_level, result, "ml");
			
			return balance;
		}

		add_order_response add_order(const new_order & order, bool validate_only = false)
		{
			using namespace details;

			input_params input;
			input.emplace("pair", order.pair);
			input.emplace("type", deal_type_to_string(order.deal));
			input.emplace("ordertype", order_type_to_string(order.type));
			
			if (order.price != 0) input.emplace("price", std::to_string(order.price));
			if (order.price2 != 0) input.emplace("price2", std::to_string(order.price2));

			input.emplace("volume", std::to_string(order.volume));

			if (order.leverage != 0) input.emplace("leverage", std::to_string(order.leverage));
			if (order.oflags != order_flags::none) input.emplace("oflags", order_flags_to_string(order.oflags));
			if (order.starttm != 0) input.emplace("starttm", timestamp_to_string(order.starttm));
			if (order.expiretm != 0) input.emplace("expiretm", timestamp_to_string(order.expiretm));
			if (order.userref != 0) input.emplace("userref", std::to_string(order.userref));
			if (validate_only) input.emplace("validate", std::to_string(true));

			const auto & response = private_method("AddOrder", input);
			const auto & result = parse_response(response);

			const auto & description = result["descr"];
			
			auto orders = get_value<std::vector<order_id_type>>(result, "txid");
			auto order_description = get_value<std::string>(description, "order");
			auto conditional_description = get_value<std::string>(description, "conditional");
	
			return add_order_response
			{
				std::move(orders),
				std::move(order_description),
				std::move(conditional_description)
			};
		}

		cancel_order_response cancel_order(const order_id_type & id)
		{
			using namespace details;

			input_params input;
			input.emplace("txid", id);

			const auto & response = private_method("CancelOrder", input);
			const auto & result = parse_response(response);

			std::uint64_t count = 0;
			bool pending = false;

			read_value(count, result, "count");
			read_value(pending, result, "pending");

			return cancel_order_response{ count, pending };
		}
#endif // KRAKEN_API_PUBLIC_ONLY 

	private:
		// helper type to make requests
		using input_params = std::map<std::string, std::string>;

		// init CURL and other stuffs
		void init()
		{
			curl_.setopt(CURLOPT_SSL_VERIFYPEER, 1L);
			curl_.setopt(CURLOPT_SSL_VERIFYHOST, 2L);
			//curl_.setopt(CURLOPT_CAINFO, "cacert.pem");
			curl_.setopt(CURLOPT_USERAGENT, "Kraken C++ API Client");
			curl_.setopt(CURLOPT_POST, 1L);
			
			nonce_ = (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())).count();
		}

		// makes public method to kraken.com 
		std::string public_method(
			const std::string& method,
			const KAPI::input_params& input = KAPI::input_params())
		{
			// build method URL
			std::string path = "/" + version_ + "/public/" + method;
			std::string method_url = url_ + path;
			curl_.setopt(CURLOPT_URL, method_url.c_str());

			// build postdata 
			std::string postdata = build_query(input);
			curl_.setopt(CURLOPT_POSTFIELDS, postdata.c_str());

			// reset the http header
			curl_.setopt(CURLOPT_HTTPHEADER, nullptr);

			return curl_.perform();
		}

#ifndef KRAKEN_API_PUBLIC_ONLY 
		// makes private method to kraken.com
		std::string private_method(
			const std::string& method,
			const KAPI::input_params& input = KAPI::input_params())
		{
			// build method URL
			std::string path = "/" + version_ + "/private/" + method;
			std::string method_url = url_ + path;

			curl_.setopt(CURLOPT_URL, method_url.c_str());

			// create a nonce and and postdata 
			std::string nonce = create_nonce();
			std::string postdata = "nonce=" + nonce;

			// if 'input' is not empty generate other postdata
			if (!input.empty())
			{
				postdata = postdata + "&" + build_query(input);
			}

			curl_.setopt(CURLOPT_POSTFIELDS, postdata.c_str());

			std::vector<std::string> header_strings =
			{
				"API-Key: " + key_,
				"API-Sign: " + signature(path, nonce, postdata)
			};

			return curl_.perform_header_in(header_strings);
		}

		// create signature for private requests
		std::string signature(
			const std::string& path,
			const std::string& nonce,
			const std::string& postdata) const
		{
			using namespace details;

			// add path to data to encrypt
			std::vector<unsigned char> data(path.begin(), path.end());

			// concatenate nonce and postdata and compute SHA256
			std::vector<unsigned char> nonce_postdata = sha256(nonce + postdata);

			// concatenate path and nonce_postdata (path + sha256(nonce + postdata))
			data.insert(data.end(), nonce_postdata.begin(), nonce_postdata.end());

			// and compute HMAC
			return b64_encode(hmac_sha512(data, b64_decode(secret_)));
		}
#endif // KRAKEN_API_PUBLIC_ONLY 

		//------------------------------------------------------------------------------
		// builds a query string from KAPI::Input (a=1&b=2&...)
		static std::string build_query(const input_params& input)
		{
			std::ostringstream oss;
			input_params::const_iterator it = input.begin();
			for (; it != input.end(); ++it)
			{
				if (it != input.begin()) oss << '&';  // delimiter
				oss << it->first << '=' << it->second;
			}

			return oss.str();
		}

		//------------------------------------------------------------------------------
		// helper function to create a nonce:
		std::string create_nonce()
		{
			return std::to_string(nonce_++);
		}

		std::string key_;     // API key
		std::string secret_;  // API secret
		std::string url_;     // API base URL
		std::string version_; // API version

		curl::curl_wrapper curl_;
		
		std::atomic<std::uint64_t> nonce_ {0};
		
		// disallow copying
		KAPI(const KAPI&) = delete;
		KAPI& operator=(const KAPI&) = delete;
	};

}; // namespace Kraken