/* Copyright (c) 2018-2022 Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#include <boost/redis.hpp>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/consign.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/signal_set.hpp>

#include <iostream>

#if defined(BOOST_ASIO_HAS_CO_AWAIT)

namespace asio = boost::asio;
namespace resp3 = boost::redis::resp3;
using boost::redis::config;
using boost::redis::any_adapter;
using boost::asio::signal_set;
using boost::system::error_code;
using boost::redis::generic_flat_response;
using namespace std::chrono_literals;

auto session(asio::ip::tcp::socket socket) -> asio::awaitable<void>
{
   generic_flat_response req;
   any_adapter adapter{req};

   resp3::parser p{};
   std::string buffer;
   auto dbuf = asio::dynamic_buffer(buffer);
   for (;;) {
      auto mbuf = dbuf.prepare(1024);
      auto const n = co_await socket.async_read_some(mbuf);
      dbuf.commit(n);

      error_code ec;
      if (!resp3::parse(p, buffer, adapter, ec))
         continue;

      if (ec)
         std::cout << "Error: " << ec.message() << std::endl;

      co_await asio::async_write(socket, asio::buffer("+PONG\r\n", 7));

      req.value().clear();
      dbuf.consume(n);
   }
}

// Listens for tcp connections.
auto listener() -> asio::awaitable<void>
{
   try {
      auto ex = co_await asio::this_coro::executor;
      asio::ip::tcp::acceptor acc(ex, {asio::ip::tcp::v4(), 55555});
      for (;;)
         asio::co_spawn(ex, session(co_await acc.async_accept()), asio::detached);
   } catch (std::exception const& e) {
      std::clog << "Listener: " << e.what() << std::endl;
   }
}

// Called from the main function (see main.cpp)
auto co_main(config) -> asio::awaitable<void>
{
   auto ex = co_await asio::this_coro::executor;
   asio::co_spawn(ex, listener(), asio::detached);

   signal_set sig_set(ex, SIGINT, SIGTERM);
   co_await sig_set.async_wait();
}

#endif  // defined(BOOST_ASIO_HAS_CO_AWAIT)
