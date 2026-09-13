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

void to_int(std::size_t& i, std::string_view sv, system::error_code& ec)
{
   auto const res = std::from_chars(sv.data(), sv.data() + std::size(sv), i);
   if (res.ec != std::errc())
      ec = error::not_a_number;
}

parser::parser() { reset(); }

void parser::rewind()
{
   consumed_ = 0;
}

void parser::reset()
{
   header_ = {};
   depth_ = 0;
   sizes_ = default_sizes;
   bulk_length_ = default_bulk_length;
   bulk_ = type::invalid;
   consumed_ = 0;
   last_was_r_ = false;
}

std::size_t parser::get_consumed() const noexcept { return consumed_; }

bool parser::done() const noexcept
{
   return depth_ == 0 &&
          bulk_ != type::invalid &&
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

bool parser::is_delimiter(std::string_view data) const noexcept
{
   if (data[consumed_] != '\n')
      return false;

   // If this is the first element in the buffer we can't look at the previous
   // element to check if it is a '\r'.
   if (consumed_ == 0u)
      return last_was_r_;

   return data[consumed_ - 1] == '\r';
}

std::string_view parser::search_sep(std::string_view data, system::error_code& ec)
{
   // A resp3 header has the form
   //
   //   'c<data>\r\n'
   //
   // Returns the size of the data part.
   std::size_t const start = header_.empty() ? consumed_ + 1 : consumed_;
   for (; consumed_ < data.size(); ++consumed_) {
      // TODO: This is needed only for bulk types and aggregates..
      if (header_.size() < header_type::static_capacity) {
         header_.push_back(data[consumed_]);
      }

      if (is_delimiter(data)) {
         if (header_.size() < 3u) {
            ec = redis::error::invalid_data_type;
            return {};
         }

         consumed_ += 1;
         bulk_ = to_type(header_.front());
         auto const data_size = consumed_ - start;
         return data.substr(start,  data_size);
      }
   }

   last_was_r_ = data.back() == '\r';
   auto const data_size = consumed_ - start;
   return data.substr(start, data_size);
}

auto parser::write(std::string_view view, system::error_code& ec) noexcept -> parser::result
{
   // TODO: Can we avoid this check?
   if (view.size() == 0u)
     return {};

   switch (bulk_) {
      case type::invalid:
      {
         auto const data = search_sep(view, ec);
         if (ec) {
            return {}; // Error.
         }

         if (bulk_ == type::invalid) {
            auto const t = to_type(header_.front());
            return {consumed_, {t, 1, depth_, data}};
         }

         auto const ret = process_header(data, ec);
         if (ec)
            return {};

         header_.clear();
         if (!is_bulk(bulk_)) { // TODO
            return {consumed_, ret};
         }
      }
         [[fallthrough]];

      default:  // Handles bulk.
      {
         auto const span = bulk_length_ + 2;
         if ((std::size(view) - consumed_) < span)
            return {};  // Needs more data to proceeed.

         auto const bulk_view = view.substr(consumed_, bulk_length_);
         node_type const ret = {bulk_, 1, depth_, bulk_view};
         bulk_ = type::invalid;
         commit_elem();

         consumed_ += span;
         return {consumed_, ret};
      }
   }
}

std::string_view parser::get_header_content() const noexcept
{
   BOOST_ASSERT(header_.size() >= 3u);
   return std::string_view(header_.data() + 1, header_.size() - 3);
}

auto parser::process_header(std::string_view const& data, system::error_code& ec) -> parser::node_type
{
   switch (bulk_) {
      case type::streamed_string_part:
      {
         auto const num = get_header_content();
         to_int(bulk_length_, num, ec);
         if (ec)
            return {};

         if (bulk_length_ == 0) {
            auto const ret = node_type{type::streamed_string_part, 1, depth_, {}};
            sizes_[depth_] = 1;  // We are done.
            bulk_ = type::invalid;
            commit_elem();
            return ret;
         } else {
            bulk_ = type::streamed_string_part;
            return {};
         }
      } break;
      case type::blob_error:
      case type::verbatim_string:
      case type::blob_string:
      {
         if (header_.at(0) == '?') {
            // NOTE: This can only be triggered with blob_string.
            // Trick: A streamed string is read as an aggregate of
            // infinite length. When the streaming is done the server
            // is supposed to send a part with length 0.
            sizes_[++depth_] = (std::numeric_limits<std::size_t>::max)();
            return {type::streamed_string, 0, depth_, {}};
         } else {
            auto const num = get_header_content();
            to_int(bulk_length_, num, ec);
            if (ec)
               return {};

            return {};
         }
      } break;
      case type::boolean:
      {
         auto const value = get_header_content();
         if (std::empty(value)) {
            ec = error::empty_field;
            return {};
         }

         if (value.at(0) != 'f' && value.at(0) != 't') {
            ec = error::unexpected_bool_value;
            return {};
         }

         node_type const ret{bulk_, 1, depth_, value};
         commit_elem();
         return ret;
      } break;
      case type::doublean:
      case type::big_number:
      case type::number:
      {
         if (std::empty(data)) {
            ec = error::empty_field;
            return {};
         }

         node_type const ret = {bulk_, 1, depth_, data};
         commit_elem();
         return ret;
      } break;
      case type::simple_error:
      case type::simple_string:
      case type::null:
      {
         node_type const ret = {bulk_, 1, depth_, data};
         commit_elem();
         return ret;
      } break;
      case type::push:
      case type::set:
      case type::array:
      case type::attribute:
      case type::map:
      {
         auto const num = get_header_content();
         std::size_t l = static_cast<std::size_t>(-1);
         to_int(l, num, ec);
         if (ec)
            return {};

         node_type const ret = {bulk_, l, depth_, {}};
         if (l == 0) {
            commit_elem();
         } else {
            if (depth_ == max_embedded_depth) {
               ec = error::exceeeds_max_nested_depth;
               return {};
            }

            ++depth_;

            sizes_[depth_] = l * element_multiplicity(bulk_);
         }
         return ret;
      } break;
      default:
      {
         ec = error::invalid_data_type;
         return {};
      }
   }
}

bool parser::is_parsing() const noexcept
{
   auto const v = depth_ == 0 && sizes_ == default_sizes && bulk_length_ == default_bulk_length &&
                  bulk_ == type::invalid && consumed_ == 0;

   return !v;
}

}  // namespace boost::redis::resp3
