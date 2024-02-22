/*
Market data collector for crypto exchanges.
https://github.com/ilia-funtov/crypto-market-data-collector

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
Copyright (c) 2022 Ilia Funtov.
*/

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <openssl/ssl.h>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>

namespace websocket_wrapper
{
	class websocket
	{
	public:
		enum class control_message_type : unsigned int
		{
			ping,
			pong
		};

		using error_handler_t = std::function<void(const std::exception &)>;
		using read_handler_t = std::function<void(const std::string &)>;
		using ping_handler_t = std::function<void(control_message_type)>;

		websocket(
			const std::string & api_address,
			unsigned int port,
			const std::string & handshake_target) :
			_api_address(api_address),
			_port(port),
			_handshake_target(handshake_target)
		{
			_ctx.set_options(
				boost::asio::ssl::context::default_workarounds |
				boost::asio::ssl::context::no_sslv2 |
				boost::asio::ssl::context::no_sslv3);

			_ctx.set_default_verify_paths();
		}

		websocket(const websocket &) = delete;
		websocket& operator = (const websocket &) = delete;
		websocket(websocket &&) = delete;
		websocket& operator = (websocket &&) = delete;

		~websocket()
		{
			stop();
		}

		void run(
			read_handler_t read_handler,
			error_handler_t error_handler,
			ping_handler_t ping_handler = ping_handler_t{})
		{
			std::lock_guard<std::mutex> lock(_start_stop_mtx);
			if (_loop_thread.joinable())
			{
				throw std::runtime_error("Websocket loop thread is running already.");
			}

			_running = true;

			try
			{
				_loop_thread = std::thread([this, read_handler, error_handler, ping_handler]()
				{
					work_loop(read_handler, error_handler, ping_handler);
				});
			}
			catch (...)
			{
				_running = false;
				throw;
			}
		}

		void stop()
		{
			std::lock_guard<std::mutex> lock(_start_stop_mtx);

			_running = false;

			if (_loop_thread.joinable())
			{
				_loop_thread.join();
			}

			_internal_context.reset();
		}

		void write(const std::string & str)
		{
			if (!_running)
				throw std::runtime_error("Websocket is not running.");

			if (!execute_on_websocket_object<socket_t>([&str](socket_t & ws) { ws.write(boost::asio::buffer(str)); }))
			{
				std::lock_guard<std::mutex> lock(_write_mtx);
				_to_write.push_back(str);
			}
		}

		bool is_open() const
		{
			bool open = false;

			const_cast<websocket*>(this)->execute_on_websocket_object<const socket_t>(
				[&open](const socket_t & ws) { open = ws.is_open(); }
			);

			return open;
		}

		void ping()
		{
			execute_on_websocket_object<socket_t>([](socket_t & ws) { ws.ping({}); });
		}
	private:
		using tcp = boost::asio::ip::tcp;
		using socket_t = boost::beast::websocket::stream<boost::asio::ssl::stream<tcp::socket>>;

		struct internal_context
		{
			boost::asio::io_context io_context;
			boost::beast::multi_buffer buffer;
			std::unique_ptr<socket_t> ws;
		};

		using internal_context_ptr = std::shared_ptr<internal_context>;

		template <typename socket_type>
		bool execute_on_websocket_object(std::function<void(socket_type&)> func)
		{
			auto internal_context = std::atomic_load(&_internal_context);
			if (internal_context && internal_context->ws)
			{
				func(*(internal_context->ws));
				return true;
			}

			return false;
		}

		void work_loop(
			const read_handler_t read_handler,
			const error_handler_t error_handler,
			const ping_handler_t ping_handler) noexcept
		{
			while (_running)
			{
				try
				{
					auto internal_context = std::make_shared<struct internal_context>();
					internal_context->ws = std::make_unique<socket_t>(internal_context->io_context, _ctx);

					{
						tcp::resolver resolver{ internal_context->io_context };

						const auto results = resolver.resolve(_api_address, std::to_string(_port));

						boost::asio::connect(internal_context->ws->next_layer().next_layer(), results.begin(), results.end());
					}

					if (!SSL_set_tlsext_host_name(internal_context->ws->next_layer().native_handle(), _api_address.c_str()))
					{
						throw std::runtime_error("SSL_set_tlsext_host_name failed");
					}

					internal_context->ws->next_layer().handshake(boost::asio::ssl::stream_base::client);

					internal_context->ws->handshake(_api_address, _handshake_target);

					std::atomic_store(&_internal_context, internal_context);

					{
						std::vector<std::string> to_write;

						{
							std::lock_guard<std::mutex> lock(_write_mtx);
							to_write = std::move(_to_write);
						}

						for (const auto & item : to_write)
						{
							internal_context->ws->write(boost::asio::buffer(item));
						}
					}

					if (ping_handler)
					{
						internal_context->ws->control_callback([ping_handler](
							boost::beast::websocket::frame_type type,
							boost::beast::string_view)
						{
							if (type == boost::beast::websocket::frame_type::ping)
							{
								ping_handler(control_message_type::ping);
							}
							else if (type == boost::beast::websocket::frame_type::pong)
							{
								ping_handler(control_message_type::pong);
							}
						});
					}

					internal_context->ws->async_read(
						internal_context->buffer,
						[internal_context, read_handler, error_handler, this](const boost::beast::error_code & ec, std::size_t)
					{
						async_read_func(internal_context, read_handler, error_handler, ec);
					});

					while (_running)
					{
						const auto num_executed = internal_context->io_context.run_one_for(check_running_period);
						if (num_executed == 0 && internal_context->io_context.stopped())
						{
							break;
						}
					}

					if (!_running)
					{
						internal_context->io_context.poll(); // handle last operation from IO queue
					}
				}
				catch (const std::exception & e)
				{
					error_handler(e);
					std::this_thread::yield();
				}
			}
		}

		void async_read_func(
			internal_context_ptr internal_context,
			const read_handler_t read_handler,
			const error_handler_t error_handler,
			const boost::beast::error_code & ec) noexcept
		{
			try
			{
				if (ec)
				{
					error_handler(std::system_error(ec));
					return;
				}

				if (!internal_context)
					return;

				read_handler(buffers_to_string(internal_context->buffer.data()));

				if (!_running && internal_context->ws->is_open())
				{
					internal_context->ws->async_close(
						boost::beast::websocket::close_code::normal,
						[internal_context, error_handler](const boost::beast::error_code & ec)
					{
						if (ec) error_handler(std::system_error(ec));
					});

					return;
				}

				boost::beast::multi_buffer new_buffer;
				std::swap(internal_context->buffer, new_buffer);

				internal_context->ws->async_read(
					internal_context->buffer,
					[internal_context, read_handler, error_handler, this](const boost::beast::error_code & ec, std::size_t)
				{
					async_read_func(internal_context, read_handler, error_handler, ec);
				});
			}
			catch (const std::exception & exc)
			{
				error_handler(exc);
			}
		}

		const std::chrono::seconds check_running_period = std::chrono::seconds(1);

		const std::string _api_address;
		const unsigned int _port;
		const std::string _handshake_target;

		boost::asio::ssl::context _ctx {boost::asio::ssl::context::tls};

		std::thread _loop_thread;
		std::mutex _start_stop_mtx;
		std::atomic_bool _running{ false };

		std::mutex _write_mtx;
		std::vector<std::string> _to_write;

		internal_context_ptr _internal_context;
	};
} // namespace websocket_wrapper