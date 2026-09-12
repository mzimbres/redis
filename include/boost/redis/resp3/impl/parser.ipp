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

void parser::reset()
{
   header_ = {};
   depth_ = 0;
   sizes_ = default_sizes;
   bulk_length_ = default_bulk_length;
   bulk_ = type::invalid;
   consumed_ = 0;
}

std::size_t parser::get_consumed() const noexcept { return consumed_; }

bool parser::done() const noexcept
{
   return depth_ == 0 && bulk_ == type::invalid && consumed_ != 0;
}

void parser::commit_elem() noexcept
{
   --sizes_[depth_];
   while (sizes_[depth_] == 0) {
      --depth_;
      --sizes_[depth_];
   }
}

bool parser::search_sep(std::string_view data, system::error_code& ec)
{
  // A resp3 header has the form
  //
  //   'c<data>\r\n'
  //
  // where both c and <data> do contain neither '\r' nor '\n'. Therefore in the
  // loop below we just have to wait for '\n'. If bad input is sent, the
  // processing of <data> above will fail.
  for (; consumed_ < data.size(); ++consumed_) {
     header_.push_back(data[consumed_]);

     if (data[consumed_] == '\n') {
        if (header_.size() < 3u) {
           ec = redis::error::invalid_data_type;
           return false;
        }

        consumed_ += 1;
        return true;
     }
  }

  return false;
}

auto parser::write(std::string_view view, system::error_code& ec) noexcept -> parser::result
{
   switch (bulk_) {
      case type::invalid:
      {
         auto const sep_res = search_sep(view, ec);
         if (ec || !sep_res) {
           return {}; // Needs more or error ocurred.
         }

         auto const ret = process_header(ec);
         if (ec)
            return {};

         header_.clear();
         if (!bulk_expected()) {
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

auto parser::process_header(system::error_code& ec) -> parser::node_type
{
   BOOST_ASSERT(!bulk_expected());

   auto const t = to_type(header_.front());
   switch (t) {
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

            bulk_ = t;
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

         node_type const ret{t, 1, depth_, value};
         commit_elem();
         return ret;
      } break;
      case type::doublean:
      case type::big_number:
      case type::number:
      {
         auto const num = get_header_content();
         if (std::empty(num)) {
            ec = error::empty_field;
            return {};
         }

         node_type const ret = {t, 1, depth_, num};
         commit_elem();
         return ret;
      } break;
      case type::simple_error:
      case type::simple_string:
      case type::null:
      {
         auto const num = get_header_content();
         node_type const ret = {t, 1, depth_, num};
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

         node_type const ret = {t, l, depth_, {}};
         if (l == 0) {
            commit_elem();
         } else {
            if (depth_ == max_embedded_depth) {
               ec = error::exceeeds_max_nested_depth;
               return {};
            }

            ++depth_;

            sizes_[depth_] = l * element_multiplicity(t);
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
