/* Copyright (c) Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#ifndef BOOST_REDIS_RESP3_HEADER_PARSER_HPP
#define BOOST_REDIS_RESP3_HEADER_PARSER_HPP

#include <boost/redis/resp3/node.hpp>
#include <boost/system/error_code.hpp>
#include <boost/redis/error.hpp>

#include <string_view>
#include <cctype>

namespace boost::redis::resp3::detail {

struct header_parser {
   type type_ = type::invalid;
   std::size_t size_ = 0;
   std::size_t pos_ = 0;
   std::size_t last_cr_pos_ = 0;
   std::size_t last_lf_pos_ = 0;
   std::size_t sign_ = 1;

   bool done() const noexcept;
   bool empty() const noexcept { return pos_ == 0u; }
   void on_digit(char c, system::error_code&);
   void add(char c, system::error_code& ec);
   void set_type(char c, system::error_code& ec);
   node_view write_header(std::string_view data, system::error_code& ec);
   node_view write_blob(std::string_view data);

   // public
   void reset();

   // To avoid extending the return type with yet another field, the
   // node_view::depth with be set with the consumed bytes.
   node_view write(std::string_view data, system::error_code& ec);
};

}  // namespace boost::redis::resp3::detail

#endif  // BOOST_REDIS_RESP3_HEADER_PARSER_HPP
