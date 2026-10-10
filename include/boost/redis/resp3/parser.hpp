/* Copyright (c) Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#ifndef BOOST_REDIS_RESP3_PARSER_HPP
#define BOOST_REDIS_RESP3_PARSER_HPP

#include <boost/redis/resp3/node.hpp>
#include <boost/redis/resp3/detail/header_parser.hpp>
#include <boost/redis/error.hpp>

#include <boost/system/error_code.hpp>

#include <array>
#include <limits>
#include <optional>
#include <string_view>
#include <cctype>

namespace boost::redis::resp3 {

class parser {
public:
   struct result {
     // When this number is zero more data is needed.
     std::size_t consumed = 0;

     node_view node;
   };

   static constexpr std::size_t max_embedded_depth = 5;
   static constexpr std::string_view sep = "\r\n";

private:
   using sizes_type = std::array<std::size_t, max_embedded_depth + 1>;
   
   boost::redis::resp3::detail::header_parser header_{};

   // sizes_[0] = 2 because the sentinel must be more than 1.
   static constexpr sizes_type default_sizes = {
      {2, 1, 1, 1, 1, 1}
   };

   // The current depth. Simple data types will have depth 0, whereas
   // the elements of aggregates will have depth 1. Embedded types
   // will have increasing depth.
   std::size_t depth_;

   // The parser supports up to 5 levels of nested structures. The
   // first element in the sizes stack is a sentinel and must be
   // different from 1.
   sizes_type sizes_;

   // The number of bytes consumed from the buffer.
   std::size_t consumed_;

   void commit_and_return(system::error_code& ec) noexcept;

   void commit_elem() noexcept;

public:
   parser();

   // Returns true when the parser is done with the current message.
   [[nodiscard]]
   auto done() const noexcept -> bool;

   auto get_consumed() const noexcept -> std::size_t;

   auto consume(std::string_view view, system::error_code& ec) noexcept -> result;

   void reset();

   bool is_parsing() const noexcept;

   void rewind();
};

// Returns the number of bytes consumed from the buffer, where zero means more
// data is needed.
template <class Adapter>
std::size_t write(parser& p, std::string_view const& msg, Adapter& adapter, system::error_code& ec)
{
   // This if could be avoid with a state machine that jumps into the
   // correct position.
   if (!p.is_parsing())
      adapter.on_init();

   while (!p.done()) {
      auto const res = p.consume(msg, ec);
      if (ec)
         return 0;

      adapter.on_node(res.node, ec);
      if (ec)
         return 0;
   }

   adapter.on_done();
   return p.get_consumed();
}

}  // namespace boost::redis::resp3

#endif  // BOOST_REDIS_RESP3_PARSER_HPP
