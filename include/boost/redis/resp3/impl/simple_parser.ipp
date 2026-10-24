/* Copyright (c) Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#include <boost/redis/resp3/detail/simple_parser.hpp>
#include <boost/assert.hpp>

namespace boost::redis::resp3::detail {

void simple_parser::reset()
{
   type_ = type::invalid;
   size_ = 0;
   last_cr_pos_ = 0;
   last_lf_pos_ = 0;
   pos_ = 0;
   sign_ = plus_; 
}

void simple_parser::on_digit(char c, system::error_code&)
{
   switch (type_) {
      case type::number:
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
         size_ = size_ * 10 + (c - '0');
      } break;
      default: {}
   }
}

void simple_parser::add(char c, system::error_code& ec)
{
   BOOST_ASSERT(!header_done());

   switch (c) {
      case '\n':
      {
         BOOST_ASSERT(type_ != type::invalid);
         last_lf_pos_ = pos_;
      } break;

      case '\r':
      {
         last_cr_pos_ = pos_;
      } break;

      case '?':
      {
         if (pos_ == 1)
            type_ = type::streamed_string;

      } break;

      case '-':
      {
         if (type_ == type::number && pos_ == 1u)
            sign_ = minus_; 
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
         on_digit(c, ec);
      } break;

      default: {
         // TODO: We can set an error here for types that expect a digit.
      }
   }
}

void simple_parser::set_type(char c, system::error_code& ec)
{
   switch (c) {
      case ':':
         type_ = type::number;
         break;
      case ',':
         type_ = type::doublean;
         break;
      case '#':
         type_ = type::boolean;
         break;
      case '_':
         type_ = type::null;
         break;

      // Simple types with a size, that is only known after \r\n arrives.
      // We set it to max so it is not considered done. Part of this data
      // can be passed to the user as it arrives.
      case '+':
         type_ = type::simple_string;
         size_ = std::size_t(-1);
         break;
      case '-':
         type_ = type::simple_error;
         size_ = std::size_t(-1);
         break;
      case '(':
         type_ = type::big_number;
         size_ = std::size_t(-1);
         break;

      // Blob types
      case '!':
         type_ = type::blob_error;
         break;
      case '=':
         type_ = type::verbatim_string;
         break;
      case '$':
         type_ = type::blob_string;
         break;
      case ';':
         type_ = type::streamed_string_part;
         break;

      // Aggregates
      case '>':
         type_ = type::push;
         break;
      case '~':
         type_ = type::set;
         break;
      case '*':
         type_ = type::array;
         break;
      case '|':
         type_ = type::attribute;
         break;
      case '%':
         type_ = type::map;
         break;
      default:
         ec = redis::error::invalid_data_type;
   }
}

node_view simple_parser::write_header(std::string_view data, system::error_code& ec)
{
   std::size_t offset = 0;
   if (pos_ == 0u && !data.empty()) {
      set_type(data.front(), ec);
      if (ec)
         return {};

      offset = 1;
      pos_ += 1;
   }

   std::size_t i = offset;
   for (; i < data.size() && !header_done(); ++i) {
      add(data[i], ec);
      if (ec)
         return {};

      pos_ += 1;
   }

   if (header_done()) {
      switch (type_) {
         case type::streamed_string_part:
         case type::blob_error:
         case type::verbatim_string:
         case type::blob_string:
         {
            auto const view = data.substr(i, size_);

            auto end = i + size_ + 2;
            size_ = 0;
            if (end > data.size()) {
               size_ = end - data.size();
               end = data.size();
            }

            return {type_, size_, end, view};
         } break;

         case type::doublean:
         case type::big_number:
         case type::simple_error:
         case type::simple_string:
         case type::null:
         {
            size_ = 0;
            auto const view = data.substr(offset, i - offset);
            return {type_, size_, i, view};
         }

         case type::attribute:
         case type::map:
         {
            size_ *= 2u;
            return {type_, size_, i, {}};
         }
         case type::number:
         {
            return {type_, size_, i, {sign_}};
         } break;
         case type::boolean:
         default:
         {
            return {type_, size_, i, {}};
         }
      }
   } else {
      switch (type_) {
         case type::simple_string:
         case type::simple_error:
         case type::big_number:
         {
            auto const view = data.substr(offset, i - offset);
            return {type_, size_, i, view};
         } break;
         default:
         {
            // Needs more
            return {type::invalid, std::size_t(-1), i, {}};
         }
      }
   }

   BOOST_ASSERT(false);
   return {};
}

node_view simple_parser::write_blob(std::string_view view)
{
   view = view.substr(0, size_);
   size_ -= view.size();

   auto const consumed = view.size();
   if (size_ == 1) {
      view.remove_suffix(1);
   } else if (size_ == 0) {
      if (view.size() > 1) {
         view.remove_suffix(2);
      } else {
         view.remove_suffix(1);
      }
   }

   return {type_, size_, consumed, view};
}

node_view simple_parser::write(std::string_view view, system::error_code& ec)
{
   if (header_done()) {
      return write_blob(view);
   } else {
      return write_header(view, ec);
   }
}

bool simple_parser::header_done() const noexcept
{
   // TODO: Add assertts.
   //BOOST_ASSER(t == type::null != pos_ != 3);
   //BOOST_ASSER(t != type::null != pos_ < 4);
   //BOOST_ASSER(t != type::invalid);
   return (last_cr_pos_ + 1) == last_lf_pos_;
}

}  // namespace boost::redis::resp3::detail
