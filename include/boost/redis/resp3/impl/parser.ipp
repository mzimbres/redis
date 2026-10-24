/* Copyright (c) Marcelo Zimbres Silva (mzimbres@gmail.com)
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

void parser::commit_elem() noexcept
{
   --sizes_[depth_];
   while (sizes_[depth_] == 0) {
      --depth_;
      --sizes_[depth_];
   }
}


auto parser::consume(std::string_view view, system::error_code& ec) noexcept -> parser::result
{
   BOOST_ASSERT(view.size() >= consumed_);

   view.remove_prefix(consumed_);

   if (view.size() == 0u)
      return std::nullopt;

   auto ret = header_.write(view, ec);
   if (ec) {
      return {};
   }

   consumed_ += ret.depth;
   ret.depth = depth_;

   // TODO: Move this check into the swith. boolean will exit here if false and
   // in the switch if it is true. This is confusing.
   if (ret.aggregate_size == 0u) {
      commit_elem();
      header_.reset();
      return ret;
   }

   switch (ret.data_type) {
      case type::invalid:
      {
         return std::nullopt;
      } break;
      case type::boolean:
      case type::number:
      {
         commit_elem();
         header_.reset();
      } break;
      case type::array:
      case type::push:
      case type::set:
      case type::map:
      case type::attribute:
      {
         if (depth_ == max_embedded_depth) {
            ec = error::exceeeds_max_nested_depth;
            return {};
         }
         sizes_[++depth_] = ret.aggregate_size;
         header_.reset();
      } break;

      default: {}
   }

   return ret;
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
