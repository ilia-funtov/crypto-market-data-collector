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
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include <websocket_wrapper.hpp>

namespace websocket_subscriber
{
	class websocket_subscriber_base
	{
	public:
		using error_handler_t = std::function<void(const std::exception &)>;

		websocket_subscriber_base(
			error_handler_t error_handler,
			const std::string & api_address,
			unsigned int port,
			const std::string & target) :
			_error_handler(error_handler),
			_websocket(api_address, port, target)
		{
			assert(_error_handler);

			run_websocket();

			{
				std::lock_guard<std::mutex> lock(_watch_thread_mtx);
				_running = true;
				_watch_thread = std::thread([this]() { watch_thread_loop(); });
			}
		}

		virtual ~websocket_subscriber_base()
		{
			stop();
		}

		bool is_working() const noexcept
		{
			return is_init_received() && _running;
		}

		void restart()
		{
			if (is_init_received())
			{
				std::lock_guard<std::mutex> lock(_watch_thread_signal_mtx);
				if (!_restart_websocket_required)
				{
					_restart_websocket_required = true;
					_watch_thread_var.notify_one();
				}
			}
			else
			{
				_restart_websocket_required = true;
			}
		}

	protected:
		void stop()
		{
			if (!_running)
				return;

			std::lock(_watch_thread_mtx, _watch_thread_signal_mtx);

			std::lock_guard<std::mutex> lock(_watch_thread_mtx, std::adopt_lock);

			{
				std::lock_guard<std::mutex> lock_signal(_watch_thread_signal_mtx, std::adopt_lock);

				_running = false;
				_watch_thread_var.notify_one();
			}

			if (_watch_thread.joinable())
			{
				_watch_thread.join();
			}

			_websocket.stop();
		}

		virtual void init_received(bool set_flag = true) noexcept
		{
			_init_received = set_flag;
		}

		virtual bool is_init_received() const noexcept
		{
			return _init_received;
		}

		websocket_wrapper::websocket & websocket() noexcept
		{
			return _websocket;
		}
	private:
		using clock_t = std::chrono::steady_clock;

		virtual void authenticate() {}
		virtual void subscribe_events() {}
		virtual void reset_active_channels() {}
		virtual void read_handler(const std::string &) {}

		void error_handler(const std::exception & exc)
		{
			if (_error_handler)
				_error_handler(exc);

			if (!_websocket.is_open())
			{
				_restart_websocket_required = true;
			}
		}

		void run_websocket()
		{
			update_last_message_timestamp();

			_websocket.run(
				[this](const std::string & str)
				{
					try
					{
						update_last_message_timestamp();

						read_handler(str);
					}
					catch (const std::exception & exc)
					{
						error_handler(exc);
					}
				},
				[this](const std::exception & exc) { error_handler(exc); },
				[this](websocket_wrapper::websocket::control_message_type) { update_last_message_timestamp(); });
		}

		void watch_thread_loop() noexcept
		{
			unsigned int restart_attempt = 0;

			const auto wait = [this](auto predicate)
			{
				{
					std::unique_lock<std::mutex> lock(_watch_thread_signal_mtx);
					_watch_thread_var.wait_for(
						lock,
						std::chrono::seconds(watch_period),
						predicate);
				}
			};

			while (_running)
			{
				try
				{
					if (_restart_websocket_required.exchange(false))
					{
						if (restart_attempt++ >= max_restart_attempts_no_delay)
						{
							wait([this]() -> bool { return !_running; });

							if (!_running)
								break;
						}

						do_websocket_restart();
					}

					if (_websocket.is_open() && is_init_received())
					{
						if (_authenticated)
						{
							subscribe_events();
							_websocket.ping();
						}
						else
						{
							authenticate();
							_authenticated = true;

							subscribe_events();

							restart_attempt = 0;

							wait([this]() -> bool { return !_running || _restart_websocket_required; });

							if (!_running)
								break;

							continue;
						}
					}

					if (is_last_message_time_outdated())
					{
						_restart_websocket_required = true;
						continue;
					}

					wait([this]() -> bool { return !_running || _restart_websocket_required; });
				}
				catch (const std::exception & exc)
				{
					error_handler(exc);
				}
			}
		}

		void do_websocket_restart()
		{
			_websocket.stop();

			init_received(false);
			_authenticated = false;

			reset_active_channels();

			run_websocket();
		}

		void update_last_message_timestamp()
		{
			_last_message_timestamp = get_current_timestamp();
		}

		bool is_last_message_time_outdated() const
		{
			return (get_current_timestamp() - _last_message_timestamp) > 2 * watch_period * 1000;
		}

		static std::uint64_t get_current_timestamp()
		{
			const auto time = clock_t::now();
			const auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch());
			return time_ms.count();
		}

		static constexpr unsigned int watch_period = 3; // seconds
		static constexpr unsigned int max_restart_attempts_no_delay = 3;

		const error_handler_t _error_handler;

		std::atomic_bool _running{false};
		std::atomic_bool _init_received{false};
		std::atomic_bool _authenticated{false};
		std::atomic_bool _restart_websocket_required{false};

		std::atomic<std::uint64_t> _last_message_timestamp{0};

		std::mutex _watch_thread_mtx;
		std::mutex _watch_thread_signal_mtx;
		std::condition_variable _watch_thread_var;
		std::thread _watch_thread;

		websocket_wrapper::websocket _websocket;
	};
}