/* Copyright (c) 2018-2024 Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#ifndef BOOST_REDIS_RESP3_PARSER_HPP
#define BOOST_REDIS_RESP3_PARSER_HPP

#include <boost/redis/resp3/node.hpp>
#include <boost/redis/error.hpp>

#include <boost/system/error_code.hpp>

#include <array>
#include <limits>
#include <optional>
#include <string_view>
#include <cctype>

namespace boost::redis::resp3 {

namespace detail {

inline
void add_digit(std::size_t& result, unsigned char c, system::error_code& ec)
{

   if (!std::isdigit(c)) {
      ec = redis::error::not_a_number;
      return;
   }

   // TODO: Use this to check for overflow.
   //static constexpr std::size_t uint64_digits = (std::numeric_limits<std::uint64_t>::digits10);
   result = result * 10 + (c - '0');
}

struct header {
   type t = type::invalid;
   std::size_t size = 0;
   std::size_t last_r_pos_ = 0;
   std::size_t pos_ = 0;
   bool is_streamed_string = false;
   bool done_ = false;

   bool done() const noexcept
   {
      return done_;
   }

   void reset()
   {
      t = type::invalid;
      size = 0;
      last_r_pos_ = 0;
      pos_ = 0;
      is_streamed_string = false;
      done_ = false;
   }

   bool empty() const noexcept
   {
      return t == type::invalid;
   }

   bool add(unsigned char c, system::error_code& ec)
   {
      BOOST_ASSERT(!done_);

      if (pos_ == 0) {
         t = to_type(c);
         if (t == type::invalid) {
            ec = redis::error::invalid_data_type;
            return false;
         }
      } else {
         switch (c) {
            case '\n':
            {
               done_ = t != type::invalid && (last_r_pos_ + 1u) == pos_;
               return done_;
            } break;

            case '\r':
            {
               last_r_pos_ = pos_;
            } break;

            case '?':
            {
               if (pos_ == 1)
                  is_streamed_string = true;

            } break;

            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            {
               switch (t) {
                  case type::streamed_string_part:
                  case type::blob_error:
                  case type::verbatim_string:
                  case type::blob_string:
                  case type::push:
                  case type::set:
                  case type::array:
                  case type::attribute:
                  case type::map:
                  {
                     // TODO: Use this to check for overflow.
                     //static constexpr std::size_t uint64_digits = (std::numeric_limits<std::uint64_t>::digits10);
                     size = size * 10 + (c - '0');
                  } break;
                  default: {}
               }
            } break;
         }
      }

      pos_ += 1;
      return false;
   }
};

}

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
   using sizes_type = std::array<std::size_t, max_embedded_depth + 1>;
   
   detail::header header_{};

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

   // The type of the next bulk. Contains type::invalid if no bulk is
   // expected.
   type bulk_;

   // The number of bytes consumed from the buffer.
   std::size_t consumed_;

   // Returns the number of bytes that have been consumed.
   auto process_header(std::string_view const& data, system::error_code& ec) -> node_type;

   void commit_elem() noexcept;

   std::string_view search_sep(std::string_view data, system::error_code& ec);

   bool is_delimiter(std::string_view data) const noexcept;

public:
   parser();

   // Returns true when the parser is done with the current message.
   [[nodiscard]]
   auto done() const noexcept -> bool;

   auto get_consumed() const noexcept -> std::size_t;

   auto write(std::string_view view, system::error_code& ec) noexcept -> result;

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
      auto const res = p.write(msg, ec);
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
