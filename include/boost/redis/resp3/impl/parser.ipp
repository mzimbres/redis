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

void parser::commit_and_return(system::error_code& ec) noexcept
{
   if (header_.size_ == 0u || header_.type_ == type::number) {
      commit_elem();
      header_.reset();
   } else if (is_aggregate(header_.type_)) {
      if (depth_ == max_embedded_depth) {
         ec = error::exceeeds_max_nested_depth;
         return;
      }
      sizes_[++depth_] = header_.size_;
      header_.reset();
   }
}

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
   // TODO: Can we avoid this check?
   if (view.size() == 0u)
     return {};

   view.remove_prefix(consumed_); // TODO: Let the caller remove the prefix.

   if (header_.done()) {
      view = view.substr(0, header_.size_);
      consumed_ += view.size();
      header_.size_ -= view.size();

      auto ret = header_.make_node();
      ret.depth = depth_;
      ret.value = view;
      if (ret.aggregate_size == 0u) {
         commit_elem();
         header_.reset();
      }

      return ret;
   } else {
      auto res = header_.write(view, ec);
      if (ec) {
         return {};
      }

      consumed_ += res.depth;

      if (res.data_type == type::invalid) {
         return std::nullopt;
      }

      res.depth = depth_;
      commit_and_return(ec);
      return res;
   }
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
