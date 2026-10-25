/* Copyright (c) Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#ifndef BOOST_REDIS_RESP3_SIMPLE_PARSER_HPP
#define BOOST_REDIS_RESP3_SIMPLE_PARSER_HPP

#include <boost/redis/resp3/node.hpp>
#include <boost/redis/error.hpp>

#include <string_view>
#include <cctype>

namespace boost::redis::resp3::detail {

// Parses REPS3 non-aggregate types.
class simple_parser {
private:
   type type_ = type::invalid;
   std::size_t size_ = 0;
   std::size_t pos_ = 0;
   std::size_t last_cr_pos_ = 0;
   std::size_t last_lf_pos_ = 0;
   char const* sign_ = "+";

   auto header_done() const noexcept -> bool;
   auto on_sign(char const* value, system::error_code& ec) -> void;
   auto on_digit(char c, system::error_code&) -> void;
   auto on_bool_value(std::size_t c, system::error_code&) -> void;
   auto add(char c, system::error_code& ec) -> void;
   auto set_type(char c, system::error_code& ec) -> void;
   auto handle_header(std::string_view data, system::error_code& ec) -> node_view;
   auto handle_blob_part(std::string_view data) -> node_view;

public:
   auto reset() -> void;
   auto empty() const noexcept  -> bool{ return pos_ == 0u; }

   // To avoid extending the return type with yet another field, the
   // node_view::depth with be set with the consumed bytes.
   //
   // Return value
   //
   // - number:  { type::number,  <value>,   <bytes-consumed>, "+"/"-" };
   // - boolean: { type::boolean, undefined, <bytes-consumed>, "t"/"f" };
   //
   auto write(std::string_view data, system::error_code& ec) -> node_view;
};

}  // namespace boost::redis::resp3::detail

#endif  // BOOST_REDIS_RESP3_SIMPLE_PARSER_HPP
