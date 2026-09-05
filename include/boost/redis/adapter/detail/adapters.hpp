/* Copyright (c) 2018-2025 Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#ifndef BOOST_REDIS_ADAPTER_ADAPTERS_HPP
#define BOOST_REDIS_ADAPTER_ADAPTERS_HPP

#include <boost/redis/adapter/result.hpp>
#include <boost/redis/error.hpp>
#include <boost/redis/resp3/flat_tree.hpp>
#include <boost/redis/resp3/node.hpp>
#include <boost/redis/resp3/serialization.hpp>
#include <boost/redis/resp3/type.hpp>
#include <boost/redis/response.hpp>

#include <boost/assert.hpp>

#include <array>
#include <charconv>
#include <deque>
#include <forward_list>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// See https://stackoverflow.com/a/31658120/1077832
#ifdef _LIBCPP_VERSION
#else
#include <cstdlib>
#endif

namespace boost::redis::adapter::detail {

// Exclude bools, char and charXY_t types
template <class T> struct is_integral_number : std::is_integral<T> { };
template <> struct is_integral_number<bool> : std::false_type { };
template <> struct is_integral_number<char> : std::false_type { };
template <> struct is_integral_number<char16_t> : std::false_type { };
template <> struct is_integral_number<char32_t> : std::false_type { };
template <> struct is_integral_number<wchar_t> : std::false_type { };
#ifdef __cpp_char8_t
template <> struct is_integral_number<char8_t> : std::false_type { };
#endif

template <class T, bool = is_integral_number<T>::value>
struct converter;

template <class T>
struct converter<T, true> {
   template <class String>
   static void apply(T& i, resp3::basic_node<String> const& node, system::error_code& ec)
   {
      switch (node.data_type) {
         case resp3::type::number:
         {
            i = node.aggregate_size;
            if (node.value == "-") {
               if constexpr (std::is_signed<T>::value) {
                  i *= -1;
               } else {
                  ec = redis::error::not_a_number;
                  return;
               }
            }
         } break;
         default:
         {
            auto const res = std::from_chars(node.value.data(), node.value.data() + node.value.size(), i);
            if (res.ec != std::errc())
               ec = redis::error::not_a_number;
         }
      }
   }
};

template <>
struct converter<bool, false> {
   template <class String>
   static void apply(bool& t, resp3::basic_node<String> const& node, system::error_code&)
   {
      t = node.aggregate_size == 1;
   }
};

template <>
struct converter<double, false> {
   template <class String>
   static void apply(double& d, resp3::basic_node<String> const& node, system::error_code& ec)
   {
#ifdef _LIBCPP_VERSION
      // The string in node.value is not null terminated and we also
      // don't know if there is enough space at the end for a null
      // char. The easiest thing to do is to create a temporary.
      std::string const tmp{node.value.data(), node.value.data() + node.value.size()};
      char* end{};
      d = std::strtod(tmp.data(), &end);
      if (d == HUGE_VAL || d == 0)
         ec = redis::error::not_a_double;
#else
      auto const res = std::from_chars(node.value.data(), node.value.data() + node.value.size(), d);
      if (res.ec != std::errc())
         ec = redis::error::not_a_double;
#endif  // _LIBCPP_VERSION
   }
};

template <class CharT, class Traits, class Allocator>
struct converter<std::basic_string<CharT, Traits, Allocator>, false> {
   template <class String>
   static void apply(
      std::basic_string<CharT, Traits, Allocator>& s,
      resp3::basic_node<String> const& node,
      system::error_code&)
   {
      s.append(node.value.data(), node.value.size());
   }
};

template <class T>
struct from_bulk_impl {
   template <class String>
   static void apply(T& t, resp3::basic_node<String> const& node, system::error_code& ec)
   {
      if (node.data_type == resp3::type::null) {
         ec = redis::error::resp3_null;
         return;
      }

      converter<T>::apply(t, node, ec);
   }
};

template <class T>
struct from_bulk_impl<std::optional<T>> {
   template <class String>
   static void apply(
      std::optional<T>& op,
      resp3::basic_node<String> const& node,
      system::error_code& ec)
   {
      if (node.data_type == resp3::type::null) {
        op.reset();
      } else {
         op.emplace(T{});
         converter<T>::apply(op.value(), node, ec);
      }
   }
};

template <class T, class String>
void boost_redis_from_bulk(T& t, resp3::basic_node<String> const& node, system::error_code& ec)
{
   from_bulk_impl<T>::apply(t, node, ec);
}

//================================================

template <class Result>
class general_aggregate {
private:
   Result* res_;
   bool next_is_append_ = false;

public:
   explicit general_aggregate(Result* c = nullptr)
   : res_(c)
   { }

   void on_init() { }
   void on_done() { }

   template <class String>
   void on_node(resp3::basic_node<String> const& nd, system::error_code&)
   {
      BOOST_ASSERT_MSG(!!res_, "Unexpected null pointer");

      switch (nd.data_type) {
         case resp3::type::blob_error:
         case resp3::type::simple_error:
         {
            // TODO: Append data instead of overwrite.
            *res_ = error{
               nd.data_type,
               std::string{std::cbegin(nd.value), std::cend(nd.value)}
            };
         } break;
         //case resp3::type::attribute:
         case resp3::type::streamed_string_part:
         case resp3::type::doublean:
         case resp3::type::big_number:
         case resp3::type::verbatim_string:
         case resp3::type::simple_string:
         case resp3::type::blob_string:
         {
            if (!res_->has_value())
               return;

            if (!std::exchange(next_is_append_, nd.aggregate_size != 0u)) {
               res_->value().push_back({nd.data_type, 0u, nd.depth, {}});
            }

            res_->value().back().value.append(nd.value.begin(), nd.value.end());

         } break;
         default:
         {
            if (!res_->has_value())
               return;

            res_->value().push_back({
               nd.data_type,
               nd.aggregate_size,
               nd.depth,
               std::string{std::cbegin(nd.value), std::cend(nd.value)}
            });
         }
      }
   }
};

template <>
class general_aggregate<resp3::tree> {
private:
   resp3::tree* tree_ = nullptr;

public:
   explicit general_aggregate(resp3::tree* c = nullptr)
   : tree_(c)
   { }

   void on_init() { }
   void on_done() { }

   template <class String>
   void on_node(resp3::basic_node<String> const& nd, system::error_code&)
   {
      BOOST_ASSERT_MSG(!!tree_, "Unexpected null pointer");

      resp3::node tmp;
      tmp.data_type = nd.data_type;
      tmp.aggregate_size = nd.aggregate_size;
      tmp.depth = nd.depth;
      tmp.value = std::string{std::cbegin(nd.value), std::cend(nd.value)};

      tree_->push_back(std::move(tmp));
   }
};

template <>
class general_aggregate<generic_flat_response> {
private:
   generic_flat_response* tree_ = nullptr;

public:
   explicit general_aggregate(generic_flat_response* c = nullptr)
   : tree_(c)
   { }

   void on_init()
   {
      if (tree_->has_value()) {
         tree_->value().notify_init();
      }
   }
   void on_done()
   {
      BOOST_ASSERT_MSG(!!tree_, "Unexpected null pointer");
      if (tree_->has_value()) {
         tree_->value().notify_done();
      }
   }

   template <class String>
   void on_node(resp3::basic_node<String> const& nd, system::error_code&)
   {
      BOOST_ASSERT_MSG(!!tree_, "Unexpected null pointer");
      switch (nd.data_type) {
         case resp3::type::blob_error:
         case resp3::type::simple_error:
            *tree_ = error{
               nd.data_type,
               std::string{std::cbegin(nd.value), std::cend(nd.value)}
            };
            break;
         default:
            if (tree_->has_value()) {
               (**tree_).push(nd);
            }
      }
   }
};

template <>
class general_aggregate<resp3::flat_tree> {
private:
   resp3::flat_tree* tree_ = nullptr;

public:
   explicit general_aggregate(resp3::flat_tree* c = nullptr)
   : tree_(c)
   { }

   void on_init() { tree_->notify_init(); }
   void on_done() { tree_->notify_done(); }

   template <class String>
   void on_node(resp3::basic_node<String> const& nd, system::error_code&)
   {
      BOOST_ASSERT_MSG(!!tree_, "Unexpected null pointer");
      tree_->push(nd);
   }
};

template <class Node>
class general_simple {
private:
   Node* result_;

public:
   explicit general_simple(Node* t = nullptr)
   : result_(t)
   { }

   void on_init() { }
   void on_done() { }

   template <class String>
   void on_node(resp3::basic_node<String> const& nd, system::error_code&)
   {
      BOOST_ASSERT_MSG(!!result_, "Unexpected null pointer");
      switch (nd.data_type) {
         case resp3::type::blob_error:
         case resp3::type::simple_error:
            *result_ = error{
               nd.data_type,
               std::string{std::cbegin(nd.value), std::cend(nd.value)}
            };
            break;
         default:
            result_->value().data_type = nd.data_type;
            result_->value().aggregate_size = nd.aggregate_size;
            result_->value().depth = nd.depth;
            result_->value().value.assign(nd.value.data(), nd.value.size());
      }
   }
};

template <class Result>
class simple_impl {
public:
   void on_value_available(Result&) { }

   void on_init() { }
   void on_done() { }

   template <class String>
   void on_node(Result& result, resp3::basic_node<String> const& node, system::error_code& ec)
   {
      if (is_aggregate(node.data_type)) {
         ec = redis::error::expects_resp3_simple_type;
         return;
      }

      boost_redis_from_bulk(result, node, ec);
   }
};

template <class Result>
class set_impl {
private:
   typename Result::iterator hint_;

public:
   void on_value_available(Result& result) { hint_ = std::end(result); }

   void on_init() { }
   void on_done() { }

   template <class String>
   void on_node(Result& result, resp3::basic_node<String> const& nd, system::error_code& ec)
   {
      if (is_aggregate(nd.data_type)) {
         if (nd.data_type != resp3::type::set)
            ec = redis::error::expects_resp3_set;
         return;
      }

      if (nd.depth < 1) {
         ec = redis::error::expects_resp3_set;
         return;
      }

      typename Result::key_type obj{};
      boost_redis_from_bulk(obj, nd, ec);
      hint_ = result.insert(hint_, std::move(obj));
   }
};

template <class Result>
class map_impl {
private:
   using value_type = typename Result::value_type;

   typename Result::iterator current_;
   bool on_key_ = true;

   // Accumulates the key before inserting.
   std::string key_str_;

public:
   void on_value_available(Result&) {  }

   void on_init() { }
   void on_done() { }

   template <class String>
   void on_node(Result& res, resp3::basic_node<String> const& nd, system::error_code& ec)
   {
      using key_type = typename Result::key_type;
      switch (nd.data_type) {
         case resp3::type::map:
         {
            current_ = std::end(res);
         } break;
         case resp3::type::simple_string:
         case resp3::type::simple_error:
         case resp3::type::number:
         case resp3::type::doublean:
         case resp3::type::boolean:
         case resp3::type::big_number:
         case resp3::type::null:
         case resp3::type::blob_error:
         case resp3::type::verbatim_string:
         case resp3::type::blob_string:
         case resp3::type::streamed_string:
         case resp3::type::streamed_string_part:
         {
            if (nd.depth < 1u) {
               ec = redis::error::expects_resp3_map;
               return;
            }

            if (on_key_) {
               if (key_str_.empty()) {
                  // We haven't appended any data to the key yet.
                  if (nd.aggregate_size == 0u) {
                     // Key came whole. No need to accumulate in the temporary
                     // buffer.
                     key_type key{};
                     boost_redis_from_bulk(key, nd, ec);
                     current_ = res.insert(current_, value_type{key, {}});
                     on_key_ = false; // Done with the key.
                  } else {
                     // Only part of the key was received. Accumulate in the
                     // temporary buffer and wait for the remaining parts.
                     key_str_ += nd.value;
                  }
               } else {
                  // We are accumulating the key.
                  if (nd.aggregate_size == 0u) {
                     // Add any remaining data.
                     key_str_ += nd.value;

                     // Done accumulating, it can be inserted in the map now.
                     key_type key{};

                     // tmp node with a value that points to the whole key string.
                     auto tmp_node = nd;
                     tmp_node.value = key_str_; 
                     boost_redis_from_bulk(key, tmp_node, ec);
                     current_ = res.insert(current_, value_type{key, {}});
                     key_str_ = {};
                     on_key_ = false; // Done with the key.
                  } else {
                     // Another key part arrived. Accumulate and wait for the
                     // remaining ones.
                     key_str_ += nd.value;
                  }
               }
            } else {
               // Mapped type is not accumulated but passed directly to the
               // deserialization function.
               boost_redis_from_bulk(current_->second, nd, ec);
               on_key_ = nd.aggregate_size == 0u; // Done with the key.
            }

         } break;
         default:
         {
            ec = redis::error::expects_resp3_map;
         }
      }

   }
};

template <class Result>
class vector_impl {
private:
   bool next_is_append_ = false;

public:
   void on_value_available(Result&) { }

   void on_init() { }
   void on_done() { }

   template <class String>
   void on_node(Result& res, resp3::basic_node<String> const& nd, system::error_code& ec)
   {
      switch (nd.data_type) {
         case resp3::type::array:
         case resp3::type::push:
         case resp3::type::set:
         case resp3::type::map:
         case resp3::type::attribute:
         {
            res.reserve(res.size() + nd.aggregate_size);
         } break;
         case resp3::type::boolean:
         case resp3::type::null:
         case resp3::type::number:
         {
            res.push_back({});
            boost_redis_from_bulk(res.back(), nd, ec);
         } break;
         case resp3::type::big_number:
         case resp3::type::blob_error:
         case resp3::type::blob_string:
         case resp3::type::doublean:
         case resp3::type::simple_error:
         case resp3::type::simple_string:
         case resp3::type::streamed_string_part:
         case resp3::type::verbatim_string:
         {
            if (!std::exchange(next_is_append_, nd.aggregate_size != 0u)) {
               res.push_back({});
            }

            boost_redis_from_bulk(res.back(), nd, ec);
         } break;
         default:
         {
            BOOST_ASSERT_MSG(false, "RESP3 type not supported.");
         }
      }
   }
};

template <class Result>
class vector_pair {
private:
   bool on_key_ = true;

public:
   void on_value_available(Result&) { }
   void on_init() { }
   void on_done() { }

   template <class String>
   void
   on_node(
      Result& res,
      resp3::basic_node<String> const& nd,
      system::error_code& ec)
   {
      if (is_aggregate(nd.data_type)) {
         auto const m = element_multiplicity(nd.data_type);
         if (m != 2u) {
            ec = redis::error::expects_resp3_map;
            return;
         }

         // The container is not required to be empty, so take the current size
         // into account.
         res.reserve(res.size() + m * nd.aggregate_size);
         return;
      }

      BOOST_ASSERT(nd.aggregate_size == 1);

      if (nd.depth < 1) {
         ec = redis::error::expects_resp3_map;
         return;
      }

      if (on_key_) {
         res.push_back({});
         boost_redis_from_bulk(res.back().first, nd, ec);
      } else {
         boost_redis_from_bulk(res.back().second, nd, ec);
      }

      on_key_ = !on_key_;
   }
};


template <class Result>
class array_impl {
private:
   int i_ = -1;

public:
   void on_value_available(Result&) { }

   void on_init() { }
   void on_done() { }

   template <class String>
   void on_node(Result& result, resp3::basic_node<String> const& nd, system::error_code& ec)
   {
      if (is_aggregate(nd.data_type)) {
         if (i_ != -1) {
            ec = redis::error::nested_aggregate_not_supported;
            return;
         }

         if (result.size() != nd.aggregate_size) {
            ec = redis::error::incompatible_size;
            return;
         }
      } else {
         if (i_ == -1) {
            ec = redis::error::expects_resp3_aggregate;
            return;
         }

         boost_redis_from_bulk(result.at(i_), nd, ec);
      }

      ++i_;
   }
};

template <class Result>
struct list_impl {
   void on_value_available(Result&) { }

   void on_init() { }
   void on_done() { }

   template <class String>
   void on_node(Result& result, resp3::basic_node<String> const& nd, system::error_code& ec)
   {
      if (!is_aggregate(nd.data_type)) {
         if (nd.depth < 1) {
            ec = redis::error::expects_resp3_aggregate;
            return;
         }

         result.push_back({});
         boost_redis_from_bulk(result.back(), nd, ec);
      }
   }
};

//---------------------------------------------------

template <class T>
struct impl_map {
   using type = simple_impl<T>;
};

template <class Key, class Compare, class Allocator>
struct impl_map<std::set<Key, Compare, Allocator>> {
   using type = set_impl<std::set<Key, Compare, Allocator>>;
};

template <class Key, class Compare, class Allocator>
struct impl_map<std::multiset<Key, Compare, Allocator>> {
   using type = set_impl<std::multiset<Key, Compare, Allocator>>;
};

template <class Key, class Hash, class KeyEqual, class Allocator>
struct impl_map<std::unordered_set<Key, Hash, KeyEqual, Allocator>> {
   using type = set_impl<std::unordered_set<Key, Hash, KeyEqual, Allocator>>;
};

template <class Key, class Hash, class KeyEqual, class Allocator>
struct impl_map<std::unordered_multiset<Key, Hash, KeyEqual, Allocator>> {
   using type = set_impl<std::unordered_multiset<Key, Hash, KeyEqual, Allocator>>;
};

template <class Key, class T, class Compare, class Allocator>
struct impl_map<std::map<Key, T, Compare, Allocator>> {
   using type = map_impl<std::map<Key, T, Compare, Allocator>>;
};

template <class Key, class T, class Compare, class Allocator>
struct impl_map<std::multimap<Key, T, Compare, Allocator>> {
   using type = map_impl<std::multimap<Key, T, Compare, Allocator>>;
};

template <class Key, class Hash, class KeyEqual, class Allocator>
struct impl_map<std::unordered_map<Key, Hash, KeyEqual, Allocator>> {
   using type = map_impl<std::unordered_map<Key, Hash, KeyEqual, Allocator>>;
};

template <class Key, class Hash, class KeyEqual, class Allocator>
struct impl_map<std::unordered_multimap<Key, Hash, KeyEqual, Allocator>> {
   using type = map_impl<std::unordered_multimap<Key, Hash, KeyEqual, Allocator>>;
};

template <class T, class Allocator>
struct impl_map<std::vector<T, Allocator>> {
   using type = vector_impl<std::vector<T, Allocator>>;
};

template <class T, class U, class Allocator>
struct impl_map<std::vector<std::pair<T, U>, Allocator>> {
   using type = vector_pair<std::vector<std::pair<T, U>, Allocator>>;
};

template <class T, std::size_t N>
struct impl_map<std::array<T, N>> {
   using type = array_impl<std::array<T, N>>;
};

template <class T, class Allocator>
struct impl_map<std::list<T, Allocator>> {
   using type = list_impl<std::list<T, Allocator>>;
};

template <class T, class Allocator>
struct impl_map<std::deque<T, Allocator>> {
   using type = list_impl<std::deque<T, Allocator>>;
};

//---------------------------------------------------

template <class>
class wrapper;

template <class T>
class wrapper<result<T>> {
public:
   using response_type = result<T>;

private:
   response_type* result_;
   typename impl_map<T>::type impl_;
   bool called_once_ = false;

   template <class String>
   bool set_if_resp3_error(resp3::basic_node<String> const& nd) noexcept
   {
      switch (nd.data_type) {
         case resp3::type::null:
         case resp3::type::simple_error:
         case resp3::type::blob_error:
            *result_ = error{
               nd.data_type,
               {std::cbegin(nd.value), std::cend(nd.value)}
            };
            return true;
         default: return false;
      }
   }

public:
   explicit wrapper(response_type* t = nullptr)
   : result_(t)
   {
      if (result_) {
         impl_.on_value_available(result_->value());
      }
   }

   void on_init() { impl_.on_init(); }
   void on_done() { impl_.on_done(); }

   template <class String>
   void on_node(resp3::basic_node<String> const& nd, system::error_code& ec)
   {
      BOOST_ASSERT_MSG(!!result_, "Unexpected null pointer");

      if (result_->has_error())
         return;

      if (!std::exchange(called_once_, true) && set_if_resp3_error(nd))
         return;

      BOOST_ASSERT(result_);
      impl_.on_node(result_->value(), nd, ec);
   }
};

template <class T>
class wrapper<result<std::optional<T>>> {
public:
   using response_type = result<std::optional<T>>;

private:
   response_type* result_;
   typename impl_map<T>::type impl_{};
   bool called_once_ = false;

   template <class String>
   bool set_if_resp3_error(resp3::basic_node<String> const& nd) noexcept
   {
      switch (nd.data_type) {
         case resp3::type::blob_error:
         case resp3::type::simple_error:
            *result_ = error{
               nd.data_type,
               {std::cbegin(nd.value), std::cend(nd.value)}
            };
            return true;
         default: return false;
      }
   }

public:
   explicit wrapper(response_type* o = nullptr)
   : result_(o)
   { }

   void on_init() { impl_.on_init(); }
   void on_done() { impl_.on_done(); }

   template <class String>
   void on_node(resp3::basic_node<String> const& nd, system::error_code& ec)
   {
      BOOST_ASSERT_MSG(!!result_, "Unexpected null pointer");

      if (result_->has_error())
         return;

      if (set_if_resp3_error(nd))
         return;

      if (!std::exchange(called_once_, true) && nd.data_type == resp3::type::null)
         return;

      if (!result_->value().has_value()) {
         result_->value() = T{};
         impl_.on_value_available(result_->value().value());
      }

      impl_.on_node(result_->value().value(), nd, ec);
   }
};

}  // namespace boost::redis::adapter::detail

#endif  // BOOST_REDIS_ADAPTER_ADAPTERS_HPP
