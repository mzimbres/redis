/* Copyright (c) 2018-2024 Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#include <boost/redis/error.hpp>
#include <boost/redis/resp3/parser.hpp>

#include <boost/assert.hpp>

#include <charconv>
#include <cstddef>
#include <limits>
#include <algorithm>
#include <iostream>

namespace boost::redis::resp3 {

parser::parser() { reset(); }

void parser::rewind()
{
   consumed_ = 0;
}

void parser::reset()
{
   depth_ = 0;
   sizes_ = default_sizes;
   consumed_ = 0;
   header_.reset();
}

std::size_t parser::get_consumed() const noexcept { return consumed_; }

bool parser::done() const noexcept
{
   return depth_ == 0 &&
          consumed_ != 0 &&
          header_.empty();
}

auto parser::write(std::string_view view, system::error_code& ec) noexcept -> parser::result
{
   // TODO: Can we avoid this check?
   if (view.size() == 0u)
     return {};

   view.remove_prefix(consumed_); // TODO: Let the caller remove the prefix.

   if (!header_.done()) {
      auto const range = header_.write(view, ec);
      if (ec) {
         return {};
      }

      auto const data = view.substr(range.begin, range.size);
      consumed_ += range.get_consumed();

      if (!header_.done()) {
         return {consumed_, {header_.t, 1, depth_, data}};
      }

      switch (header_.t) {
         case type::streamed_string_part:
         case type::blob_error:
         case type::verbatim_string:
         case type::blob_string:
         {
            view.remove_prefix(range.get_consumed());
         } break;
         case type::boolean:
         case type::doublean:
         case type::big_number:
         case type::number:
         case type::simple_error:
         case type::simple_string:
         case type::null:
         case type::streamed_string:
         case type::push:
         case type::set:
         case type::array:
         case type::attribute:
         case type::map:
             return commit_and_return(data, ec);
         default:
         {
            BOOST_ASSERT(false);
            return {};
         }
      }
   }

   if ((header_.size + 2) > view.size()) {
      consumed_ += view.size();
      header_.size -= view.size();
   } else {
     view = view.substr(0, header_.size + 2u);
     consumed_ += view.size();
     header_.size = 0;
   }

   return commit_and_return(view, ec);
}

bool parser::is_parsing() const noexcept
{
   auto const v = depth_ == 0 &&
                  sizes_ == default_sizes &&
                  consumed_ == 0 &&
                  header_.empty();

   return !v;
}

}  // namespace boost::redis::resp3
