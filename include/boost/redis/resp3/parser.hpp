/* Copyright (c) 2018-2024 Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#ifndef BOOST_REDIS_RESP3_PARSER_HPP
#define BOOST_REDIS_RESP3_PARSER_HPP

#include <boost/redis/resp3/node.hpp>

#include <boost/system/error_code.hpp>

#include <array>
#include <limits>
#include <optional>
#include <string_view>
#include <boost/static_string.hpp>

namespace boost::redis::resp3 {

class parser {
public:
   using node_type = basic_node<std::string_view>;

   struct result {
     // When this number is zero more data is needed.
     std::size_t consumed = 0;

     node_type node;
   };

   static constexpr std::size_t max_embedded_depth = 5;
   static constexpr std::string_view sep = "\r\n";

private:
   static constexpr std::size_t uint64_digits = (std::numeric_limits<std::uint64_t>::digits10);

   using sizes_type = std::array<std::size_t, max_embedded_depth + 1>;
   using header_type = boost::static_string<1 + uint64_digits + 2>;
   
   // Stores a RESP3 header in the form "t<num>\r\n"
   header_type header_{};

   // sizes_[0] = 2 because the sentinel must be more than 1.
   static constexpr sizes_type default_sizes = {
      {2, 1, 1, 1, 1, 1}
   };
   static constexpr auto default_bulk_length = static_cast<std::size_t>(-1);

   // The current depth. Simple data types will have depth 0, whereas
   // the elements of aggregates will have depth 1. Embedded types
   // will have increasing depth.
   std::size_t depth_;

   // The parser supports up to 5 levels of nested structures. The
   // first element in the sizes stack is a sentinel and must be
   // different from 1.
   sizes_type sizes_;

   // Contains the length expected in the next bulk read.
   std::size_t bulk_length_;

   // The type of the next bulk. Contains type::invalid if no bulk is
   // expected.
   type bulk_;

   // The number of bytes consumed from the buffer.
   std::size_t consumed_;

   // Returns the number of bytes that have been consumed.
   auto process_header(system::error_code& ec) -> node_type;

   void commit_elem() noexcept;

   // The bulk type expected in the next read. If none is expected
   // returns type::invalid.
   [[nodiscard]]
   auto bulk_expected() const noexcept -> bool
   {
      return bulk_ != type::invalid;
   }

   std::string_view get_header_content() const noexcept;

   bool search_sep(std::string_view data, system::error_code& ec);

public:
   parser();

   // Returns true when the parser is done with the current message.
   [[nodiscard]]
   auto done() const noexcept -> bool;

   auto get_consumed() const noexcept -> std::size_t;

   auto write(std::string_view view, system::error_code& ec) noexcept -> result;

   void reset();

   bool is_parsing() const noexcept;
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
      auto const res = p.write(msg, ec);
      if (ec)
         return 0;

      // TODO: remove this once it is possible to pass nodes with partial data
      // to the adapters.
      if (res.consumed == 0u)
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
