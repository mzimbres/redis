/* Copyright (c) Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#include <boost/redis/adapter/adapt.hpp>
#include <boost/redis/adapter/any_adapter.hpp>
#include <boost/redis/request.hpp>
#include <boost/redis/resp3/node.hpp>
#include <boost/redis/resp3/type.hpp>
#include <boost/redis/response.hpp>

#include <boost/core/lightweight_test.hpp>

#include <iostream>
#include <string>

using namespace boost::redis;

using adapter::adapt2;
using boost::redis::resp3::detail::simple_parser;
using adapter::result;
using boost::redis::error;
using boost::redis::resp3::parser;
using boost::system::error_code;
using resp3::detail::deserialize;
using resp3::node;
using resp3::node_view;
using resp3::to_string;
using resp3::tree;
using namespace std::literals;

namespace resp3 = boost::redis::resp3;

namespace std
{

template <class Container>
bool check_container_equal(Container const& lhs, Container const& rhs)
{
   auto a = lhs.cbegin();
   auto b = rhs.cbegin();

   while (a != lhs.cend() && b != rhs.cend()) {
      if (*a != *b)
         return false;

      ++a;
      ++b;
   }

   return a == lhs.cend() && b == rhs.end();
}

template <class Key, class Value>
bool operator==(std::map<Key, Value> const& lhs, std::map<Key, Value> const& rhs)
{
   return check_container_equal(lhs, rhs);
}

template <class Key, class Value>
bool operator==(std::unordered_map<Key, Value> const& lhs, std::unordered_map<Key, Value> const& rhs)
{
   return check_container_equal(lhs, rhs);
}


template <class T>
bool operator==(std::vector<T> const& lhs, std::vector<T> const& rhs)
{
   return check_container_equal(lhs, rhs);
}

template <class Key, class Value>
std::ostream& operator<<(std::ostream& os, std::pair<Key, Value> const& p)
{
   os << "{" << p.first << ", " << p.second << "}";
   return os;
}

std::ostream& operator<<(std::ostream& os, std::string_view const& elem)
{
   os << '"';
   for (auto const c: elem) {
      if (c == '\r') {
         os << "\\r";
      } else if (c == '\n') {
         os << "\\n";
      } else {
         os << c;
      }
   }
   os << '"';

   return os;
}

template <class Container>
std::ostream& print_container(std::ostream& os, Container const& cont)
{
   if (cont.empty()) {
      os << "{}";
      return os;
   }

   auto begin = cont.begin();
   auto end = cont.end();

   os << "{";

   while (begin != end) {
      auto tmp = begin; // Workaround to deal with forward iterators only.
      if (++tmp == end)
         break;

      os << *begin << ", ";
      ++begin;
   }

   os << *begin << "}";
   return os;
}

template <class Key, class Value>
std::ostream& operator<<(std::ostream& os, std::map<Key, Value> const& m)
{
   return print_container(os, m);
}

template <class Key, class Value>
std::ostream& operator<<(std::ostream& os, std::unordered_map<Key, Value> const& m)
{
   return print_container(os, m);
}

template <class T>
std::ostream& operator<<(std::ostream& os, std::vector<T> const& v)
{
   return print_container(os, v);
}

}

namespace {

bool is_set(std::size_t pattern, std::size_t i)
{
   return (pattern & (1 << i)) != 0u;
}

std::vector<int>
make_partition2(std::size_t pattern, std::size_t msg_size)
{
   std::vector<int> ret;

   for (std::size_t i = 0; i < msg_size; ++i) {
      if (i == 0u) {
         ret.push_back(1);
      } else if (is_set(pattern, i) != is_set(pattern, i - 1)) {
         ret.push_back(1);
      } else {
         ret.back() += 1;
      }
   }

   return ret;
}

std::vector<std::string_view>
make_partition(std::string_view msg, std::vector<int> const& sizes)
{
   std::vector<std::string_view> ret;

   for (auto const size: sizes) {
      auto const part = msg.substr(0, size);
      ret.push_back(part);
      msg.remove_prefix(size);
   }

   return ret;
}

template <class Response>
void
write_to_resp(
   Response& resp,
   std::string_view msg,
   std::vector<int> const& partition)
{
     parser p;
     auto const parts = make_partition(msg, partition);
     // Uncomment to see what strings are being printed.
     //std::cout << parts << std::endl;
     auto adapter = boost_redis_adapt(resp);

     auto const size = parts.size();
     for (auto i = 0u; i < size; ++i) {
        error_code ec;
        auto const res = parse(p, parts.at(i), adapter, ec);
        BOOST_TEST_EQ(ec, error_code());
        // True only for the last element.
        BOOST_TEST(((i + 1) == size) == res);
        p.rewind();
     }
}

template <class Value>
bool check_one_value(response<Value> const& resp, Value const& value)
{
   BOOST_TEST(std::get<0>(resp).has_value());
   BOOST_TEST(!std::get<0>(resp).has_error());

   auto const& v = std::get<0>(resp).value();
   BOOST_TEST_EQ(v, value);
   return v == value;
}

// Checks all partitions of a string.
template <class Value>
bool stress_check(std::string_view msg, Value const& value)
{
     std::cout << "\n" << std::endl;
     std::size_t const n_values = (1 << (msg.size() - 1));
     for (std::size_t v = 0; v < n_values; ++v) {
        auto const partition = make_partition2(v, msg.size());
        response<Value> resp;
        write_to_resp(resp, msg, partition);
        if (!check_one_value(resp, value))
           return false;
     }

     return true;
}

void test_parse_number()
{
   { std::string_view const data = ":42\r\n";

     parser p;
     error_code ec;
     auto const res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 5);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::number);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->aggregate_size, 42u);
     BOOST_TEST_EQ(res->value, "+");
     BOOST_TEST(p.done());
   }

   { std::string_view const data = ":+42\r\n";

     parser p;
     error_code ec;
     auto const res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 6);
     BOOST_TEST_EQ(res->data_type, resp3::type::number);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->aggregate_size, 42);
     BOOST_TEST_EQ(res->value, "+");
     BOOST_TEST(p.done());
   }

   { std::string_view const data = ":-42\r\n";

     parser p;
     error_code ec;
     auto const res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 6);
     BOOST_TEST_EQ(res->data_type, resp3::type::number);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->aggregate_size, 42);
     BOOST_TEST_EQ(res->value, "-");
     BOOST_TEST(p.done());
   }

   { std::string_view const d1 = ":1234", d2 = "56789\r\n";

     parser p;
     error_code ec;
     parser::result res;

     // Part 1
     res = p.consume(d1, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 5);
     BOOST_TEST(!res.has_value());
     BOOST_TEST(!p.done());

     // Part 2
     p.rewind();
     res = p.consume(d2, ec);
     BOOST_TEST_EQ(p.get_consumed(), 7);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::number);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->aggregate_size, 123456789);
     BOOST_TEST_EQ(res->value, "+");
     BOOST_TEST(p.done());
   }

   { std::string_view const data = ":42d9\r\n";

     parser p;
     error_code ec;
     (void) p.consume(data, ec);
     BOOST_TEST_EQ(ec, error::not_a_number);
   }

   { std::string_view const data = ":\r\n";

     parser p;
     error_code ec;
     (void) p.consume(data, ec);
     BOOST_TEST_EQ(ec, error::empty_field);
   }
}

void test_simple_string_adapter()
{
   using boost::redis::resp3::parser;
   using boost::redis::error;
   using boost::system::error_code;

   auto const msg = "+a\r\n"sv;
   std::string value = "a";

   BOOST_TEST(stress_check(msg, value));
}

void test_adapter_unordered_map_string_string()
{
   using boost::redis::resp3::parser;
   using boost::redis::error;
   using boost::system::error_code;

  auto const msg = "%2\r\n+ab\r\n+cd\r\n+ef\r\n+gh\r\n"sv;
  std::unordered_map<std::string, std::string> value
   {{"ab", "cd"}, {"ef", "gh"}};

  BOOST_TEST(stress_check(msg, value));
}

void test_adapter_map_string_string()
{
   using boost::redis::resp3::parser;
   using boost::redis::error;
   using boost::system::error_code;

   { 
     // The adapter should be able to deal with duplicate values.
     auto const msg = "%2\r\n+ab\r\n+cd\r\n+ab\r\n+cd\r\n"sv;
     std::map<std::string, std::string> value
        {{"ab", "cd"}};

     BOOST_TEST(stress_check(msg, value));
   }

   { 
     auto const msg = "%2\r\n+ab\r\n+cd\r\n+ef\r\n+gh\r\n"sv;
     std::map<std::string, std::string> value
      {{"ab", "cd"}, {"ef", "gh"}};

     BOOST_TEST(stress_check(msg, value));
   }

   { std::string_view const msg =
         "*2\r\n"
             "%1\r\n"
                 "+key\r\n+value\r\n"
             "*1\r\n"
                 "+value\r\n";

     using value1_t = std::map<std::string, std::string>;
     using value2_t = std::vector<std::string>;

     response<response<value1_t, value2_t>> resp;

     auto adapter = boost_redis_adapt(resp);
     parser p;
     error_code ec;
     auto const res = parse(p, msg, adapter, ec);
     BOOST_TEST_EQ(ec, error_code());
     BOOST_TEST(res);

     auto const& map = std::get<0>(std::get<0>(resp).value()).value();
     auto const& str = std::get<1>(std::get<0>(resp).value()).value();

     value1_t const expected1 = {{"key", "value"}};
     BOOST_TEST_EQ(map, expected1);
     value2_t const expected2 = {{"value"}};
     BOOST_TEST_EQ(str, expected2);
   }
}

void test_adapter_vector_string()
{
   using boost::redis::resp3::parser;
   using boost::redis::error;
   using boost::system::error_code;

   auto const msg1 = "*2\r\n+abc\r\n+abc\r\n"sv;
   auto const msg2 = "*2\r\n$3\r\nabc\r\n$3\r\nabc\r\n"sv;
   auto const msg3 = "%1\r\n+abc\r\n+abc\r\n"sv;
   auto const msg4 = "~2\r\n+abc\r\n+abc\r\n"sv;
   auto const msg5 = ">2\r\n+abc\r\n+abc\r\n"sv;

   std::vector<std::string> const value
      {{"abc"}, {"abc"}};

   BOOST_TEST(stress_check(msg1, value));
   BOOST_TEST(stress_check(msg2, value));
   BOOST_TEST(stress_check(msg3, value));
   BOOST_TEST(stress_check(msg4, value));
   BOOST_TEST(stress_check(msg5, value));
}

void test_adapter_vector_pair_string()
{
   using boost::redis::resp3::parser;
   using boost::redis::error;
   using boost::system::error_code;

   auto const msg1 = "%1\r\n+abc\r\n+abc\r\n"sv;
   auto const msg2 = "|1\r\n+abc\r\n+abc\r\n"sv;

   std::vector<std::pair<std::string, std::string>> const value
      {{{"abc"}, {"abc"}}};

   BOOST_TEST(stress_check(msg1, value));
   BOOST_TEST(stress_check(msg2, value));
}

void test_adapter_vector_int()
{
   using boost::redis::resp3::parser;
   using boost::redis::error;
   using boost::system::error_code;

   auto const msg1 = "*2\r\n:123\r\n:456\r\n"sv;
   auto const msg2 = "%1\r\n:123\r\n:456\r\n"sv;
   auto const msg3 = "~2\r\n:123\r\n:456\r\n"sv;
   auto const msg4 = ">2\r\n:123\r\n:456\r\n"sv;

   std::vector<int> const value
      {123, 456};

   BOOST_TEST(stress_check(msg1, value));
   BOOST_TEST(stress_check(msg2, value));
   BOOST_TEST(stress_check(msg3, value));
   BOOST_TEST(stress_check(msg4, value));
}

void test_parse_simple_string()
{
   using boost::redis::resp3::parser;
   using boost::redis::error;
   using boost::system::error_code;

   { std::string_view const data = "+abcd\n";

     parser p;
     error_code ec;
     parser::result res;

     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 6);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->value, "abcd\n");
     BOOST_TEST(!p.done());
   }

   { std::string_view const data = "+ab\rd\nefg\r\n";

     parser p;
     error_code ec;
     parser::result res;

     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 11);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->value, "ab\rd\nefg");
     BOOST_TEST(p.done());
   }

   { std::string_view const d1 = "+abcdefg\r", d2 = "\n";

     parser p;
     error_code ec;
     parser::result res;

     res = p.consume(d1, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 9);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->aggregate_size, simple_parser::unknown_size);
     BOOST_TEST_EQ(res->value, "abcdefg");
     BOOST_TEST(!p.done());

     p.rewind();
     res = p.consume(d2, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 1);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->value, "");
     BOOST_TEST(p.done());
   }

   { std::string_view const d1 = "+ab\r", d2 = "cd\r\n";

     parser p;
     error_code ec;
     parser::result res;

     res = p.consume(d1, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 4);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->value, "ab"); // BUG (known), should be ab\r.
     BOOST_TEST(!p.done());

     p.rewind();
     res = p.consume(d2, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 4);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->value, "cd");
     BOOST_TEST(p.done());
   }
}

void test_parse_set()
{
   using boost::redis::resp3::parser;
   using boost::redis::error;
   using boost::system::error_code;

   { std::string_view const data = "~2\r\n+one\r\n:42\r\n";

     parser p;
     error_code ec;
     parser::result res;

     // First node
     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 4);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::set);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->aggregate_size, 2u);
     BOOST_TEST(!p.done());

     // Second node
     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 10);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res->depth, 1u);
     BOOST_TEST_EQ(res->value, "one");
     BOOST_TEST(!p.done());

     // Third node
     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 15);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::number);
     BOOST_TEST_EQ(res->depth, 1u);
     BOOST_TEST_EQ(res->aggregate_size, 42u);
     BOOST_TEST(p.done());
   }

   { std::string_view const data = "~2\r\n~2\r\n+one\r\n:42\r\n:42\r\n";

     parser p;
     error_code ec;
     parser::result res;

     // First node
     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 4);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::set);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->aggregate_size, 2u);
     BOOST_TEST(!p.done());

     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 8);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::set);
     BOOST_TEST_EQ(res->depth, 1u);
     BOOST_TEST_EQ(res->aggregate_size, 2u);
     BOOST_TEST(!p.done());

     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 14);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res->depth, 2u);
     BOOST_TEST(!p.done());

     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 19);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::number);
     BOOST_TEST_EQ(res->depth, 2u);
     BOOST_TEST(!p.done());

     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 24);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::number);
     BOOST_TEST_EQ(res->depth, 1u);
     BOOST_TEST(p.done());
   }

   { std::string_view const data = "~1\r\n~1\r\n~1\r\n~1\r\n~1\r\n~1\r\n+one\r\n";

     parser p;
     error_code ec;
     parser::result res;

     // First node
     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 4);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::set);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->aggregate_size, 1u);
     BOOST_TEST(!p.done());

     // Second node
     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 8);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::set);
     BOOST_TEST_EQ(res->depth, 1u);
     BOOST_TEST_EQ(res->aggregate_size, 1u);
     BOOST_TEST(!p.done());

     // Third node
     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 12);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::set);
     BOOST_TEST_EQ(res->depth, 2u);
     BOOST_TEST_EQ(res->aggregate_size, 1u);
     BOOST_TEST(!p.done());

     // Fourth node
     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 16);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::set);
     BOOST_TEST_EQ(res->depth, 3u);
     BOOST_TEST_EQ(res->aggregate_size, 1u);
     BOOST_TEST(!p.done());

     // Fifth node
     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 20);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::set);
     BOOST_TEST_EQ(res->depth, 4u);
     BOOST_TEST_EQ(res->aggregate_size, 1u);
     BOOST_TEST(!p.done());

     // Sixth node
     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error::exceeeds_max_nested_depth);
   }
}

void test_parse_map()
{
   using boost::redis::resp3::parser;
   using boost::redis::error;
   using boost::system::error_code;

   { std::string_view d1 = "%2\r\n+", d2 = "a", d3 = "b\r\n+cd\r\n+ef\r\n+gh\r\n";

     parser p;
     error_code ec;
     parser::result res;

     res = p.consume(d1, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 4);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::map);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->aggregate_size, 4u);
     BOOST_TEST(!p.done());

     res = p.consume(d1, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 5);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res->depth, 1u);
     BOOST_TEST_EQ(res->aggregate_size, simple_parser::unknown_size);
     BOOST_TEST(!p.done());

     p.rewind();
     res = p.consume(d2, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 1);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res->depth, 1u);
     BOOST_TEST_EQ(res->aggregate_size, simple_parser::unknown_size);
     BOOST_TEST_EQ(res->value, "a");
     BOOST_TEST(!p.done());

     p.rewind();
     res = p.consume(d3, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 3);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res->depth, 1u);
     BOOST_TEST_EQ(res->aggregate_size, 0u);
     BOOST_TEST_EQ(res->value, "b");
     BOOST_TEST(!p.done());

     // No need to process the rest for now.
   }

   { auto const data = "%2\r\n+key1\r\n+value1\r\n+key2\r\n+value2\r\n"sv;

     parser p;
     error_code ec;
     parser::result res;

     // First node
     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 4);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::map);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->aggregate_size, 4u);
     BOOST_TEST(!p.done());

     // Second node
     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 11);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res->depth, 1u);
     BOOST_TEST_EQ(res->value, "key1");
     BOOST_TEST(!p.done());

     // Third node
     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 20);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res->depth, 1u);
     BOOST_TEST_EQ(res->value, "value1");
     BOOST_TEST(!p.done());

     // Fourth node
     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 27);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res->depth, 1u);
     BOOST_TEST_EQ(res->value, "key2");
     BOOST_TEST(!p.done());

     // Fifth node
     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 36);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res->depth, 1u);
     BOOST_TEST_EQ(res->value, "value2");
     BOOST_TEST(p.done());
   }

   { std::string_view const data = "%0\r\n";

     parser p;
     error_code ec;
     parser::result res;

     // First node
     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 4);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::map);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->aggregate_size, 0u);
     BOOST_TEST(p.done());
   }

   { std::string_view const data = "%foo\r\n";

     parser p;
     error_code ec;
     parser::result res;

     // First node
     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error::invalid_data_type);
   }

   { std::string_view const data =
         "*2\r\n"
            "%1\r\n"
               "$4\r\nname\r\n$4\r\nf14e\r\n"
            "%1\r\n"
               "$4\r\nname\r\n$4\r\nf9b5\r\n";

     parser p;
     error_code ec;
     parser::result res;

     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 4);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::array);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->aggregate_size, 2u);
     BOOST_TEST(!p.done());

     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 8);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::map);
     BOOST_TEST_EQ(res->depth, 1u);
     BOOST_TEST_EQ(res->aggregate_size, 2u);
     BOOST_TEST(!p.done());

     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 18);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::blob_string);
     BOOST_TEST_EQ(res->depth, 2u);
     BOOST_TEST_EQ(res->aggregate_size, 0u);
     BOOST_TEST(!p.done());

     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 28);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::blob_string);
     BOOST_TEST_EQ(res->depth, 2u);
     BOOST_TEST_EQ(res->aggregate_size, 0u);
     BOOST_TEST(!p.done());

     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 32);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::map);
     BOOST_TEST_EQ(res->depth, 1u);
     BOOST_TEST_EQ(res->aggregate_size, 2u);
     BOOST_TEST(!p.done());

     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 42u);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::blob_string);
     BOOST_TEST_EQ(res->depth, 2u);
     BOOST_TEST_EQ(res->aggregate_size, 0u);
     BOOST_TEST(!p.done());

     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 52);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::blob_string);
     BOOST_TEST_EQ(res->depth, 2u);
     BOOST_TEST_EQ(res->aggregate_size, 0u);
     BOOST_TEST(p.done());
   }
}

void test_parse_streamed_string()
{
   using boost::redis::resp3::parser;
   using boost::redis::error;
   using boost::system::error_code;

   {
     parser p;
     error_code ec;
     parser::result res;

     res = p.consume("$?\r\n", ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::streamed_string);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->aggregate_size, 0u);
     BOOST_TEST(p.done());

     p.reset();
     res = p.consume(";5\r\n12345\r\n", ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::streamed_string_part);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->value, "12345");
     BOOST_TEST(p.done());
   }

}

void test_parse_blob_string()
{
   using boost::redis::resp3::parser;
   using boost::redis::error;
   using boost::system::error_code;

   { std::string_view const data = "$0\r\n\r\n";

     parser p;
     error_code ec;
     parser::result res;

     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 6);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::blob_string);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->value, "");
     BOOST_TEST(p.done());
   }

   { std::string_view const d1 = "$0\r", d2 = "\n\r", d3 = "\n";

     parser p;
     error_code ec;
     parser::result res;

     res = p.consume(d1, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 3);
     BOOST_TEST(!res.has_value());
     BOOST_TEST(!p.done());

     p.rewind();
     res = p.consume(d2, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 2);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::blob_string);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->value, "");
     BOOST_TEST(!p.done());

     p.rewind();
     res = p.consume(d3, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 1);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::blob_string);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->value, "");
     BOOST_TEST(p.done());
   }

   { std::string_view const data = "$11\r\nhello world\r\n<ignore>";

     parser p;
     error_code ec;
     parser::result res;

     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 18);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::blob_string);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->aggregate_size, 0u);
     BOOST_TEST_EQ(res->value, "hello world");
     BOOST_TEST(p.done());
   }

   { std::string_view const d1 = "$11\r\nhello", d2 = " world\r\n<ignore>";

     parser p;
     error_code ec;
     parser::result res;

     res = p.consume(d1, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 10);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::blob_string);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->aggregate_size, 8u);
     BOOST_TEST_EQ(res->value, "hello");
     BOOST_TEST(!p.done());

     p.rewind();
     res = p.consume(d2, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 8);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::blob_string);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->aggregate_size, 0u);
     BOOST_TEST_EQ(res->value, " world");
     BOOST_TEST(p.done());
   }

   { std::string_view const d1 = "$11\r\nboos", d2 = "t.redi", d3 = "s\r\n";

     parser p;
     error_code ec;
     parser::result res;

     res = p.consume(d1, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 9);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::blob_string);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->value, "boos");
     BOOST_TEST(!p.done());

     p.rewind();
     res = p.consume(d2, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 6);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::blob_string);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->value, "t.redi");
     BOOST_TEST(!p.done());

     p.rewind();
     res = p.consume(d3, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 3);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::blob_string);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->value, "s");
     BOOST_TEST(p.done());
   }

   { std::string_view const d1 = "$5\r\nboost\r", d2 = "\n<ignore>";

     parser p;
     error_code ec;
     parser::result res;

     res = p.consume(d1, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 10);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::blob_string);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->value, "boost");
     BOOST_TEST(!p.done());

     p.rewind();
     res = p.consume(d2, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 1);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::blob_string);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->value, "");
     BOOST_TEST(p.done());
   }

   { std::string_view const d1 = "$12", d2 = "34\r", d3 = "\n";

     parser p;
     error_code ec;
     parser::result res;

     res = p.consume(d1, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 3);
     BOOST_TEST(!res.has_value());
     BOOST_TEST(!p.done());

     p.rewind();
     res = p.consume(d2, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 3);
     BOOST_TEST(!res.has_value());
     BOOST_TEST(!p.done());

     p.rewind();
     res = p.consume(d3, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 1);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::blob_string);
     BOOST_TEST(!p.done());
   }
}

void test_parse_null()
{
   using boost::redis::resp3::parser;
   using boost::redis::error;
   using boost::system::error_code;

   { std::string_view const data = "_\r\n";

     parser p;
     error_code ec;
     auto const res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 3);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::null);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST(p.done());
   }

   { std::string_view const d1 = "_", d2 = "\r\n";

     parser p;
     error_code ec;
     parser::result res;

     res = p.consume(d1, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 1);
     BOOST_TEST(!res.has_value());
     BOOST_TEST(!p.done());

     p.rewind();
     res = p.consume(d2, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 2);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::null);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST(p.done());
   }

   { std::string_view const d1 = "_", d2 = "\r", d3 = "\n";

     parser p;
     error_code ec;
     parser::result res;

     res = p.consume(d1, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 1);
     BOOST_TEST(!res.has_value());
     BOOST_TEST(!p.done());

     p.rewind();
     res = p.consume(d2, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 1);
     BOOST_TEST(!res.has_value());
     BOOST_TEST(!p.done());

     p.rewind();
     res = p.consume(d3, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 1);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::null);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST(p.done());
   }
}

void test_parse_boolean()
{
   using boost::redis::resp3::parser;
   using boost::redis::error;
   using boost::system::error_code;

   { std::string_view const data = "#t\r\n";

     parser p;
     error_code ec;
     auto const res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 4);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::boolean);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->aggregate_size, 1);
     BOOST_TEST(p.done());
   }

   { std::string_view const data = "#f\r\n";

     parser p;
     error_code ec;
     auto const res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 4);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::boolean);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->aggregate_size, 0);
     BOOST_TEST(p.done());
   }

   { std::string_view const data = "#11\r\n";

     parser p;
     error_code ec;
     (void) p.consume(data, ec);
     BOOST_TEST_EQ(ec, error::unexpected_bool_value);
   }

   { std::string_view const data = "#u\r\n";

     parser p;
     error_code ec;
     (void) p.consume(data, ec);
     BOOST_TEST_EQ(ec, error::invalid_data_type);
   }

   { std::string_view const data = "#\r\n";

     parser p;
     error_code ec;
     (void) p.consume(data, ec);
     BOOST_TEST_EQ(ec, error::empty_field);
   }

   // TODO: Test incremental parsing.
   // TODO: Test what happens when the bools contains a long string with and
   // without f/t chars. Does it fail fast enough?
}

void test_parse_double()
{
   using boost::redis::resp3::parser;
   using boost::redis::error;
   using boost::system::error_code;

   { std::string_view const data = ",inf\r\n";

     parser p;
     error_code ec;
     auto const res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 6);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::doublean);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->aggregate_size, 0u);
     BOOST_TEST_EQ(res->value, "inf");
     BOOST_TEST(p.done());
   }

   { std::string_view const data = ",-inf\r\n";

     parser p;
     error_code ec;
     auto const res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 7);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::doublean);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->aggregate_size, 0u);
     BOOST_TEST_EQ(res->value, "-inf");
     BOOST_TEST(p.done());
   }
}

void test_parse_verbatim_string()
{
   using boost::redis::resp3::parser;
   using boost::redis::error;
   using boost::system::error_code;

   { std::string_view const data = "=15\r\ntxt:Some string\r\n";

     parser p;
     error_code ec;
     parser::result res;

     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 22);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::verbatim_string);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->aggregate_size, 0u);
     BOOST_TEST_EQ(res->value, "txt:Some string");
     BOOST_TEST(p.done());
   }
}

void test_parse_big_number()
{
   using boost::redis::resp3::parser;
   using boost::redis::error;
   using boost::system::error_code;

   { std::string_view const data = "(\r\n";

     parser p;
     error_code ec;
     parser::result res;

     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error::empty_field);
   }
}

void test_parse_array()
{
   using boost::redis::resp3::parser;
   using boost::redis::error;
   using boost::system::error_code;

   { std::string_view const data = "*2\r\n$5\r\nhello\r\n:\r\n";

     parser p;
     error_code ec;
     parser::result res;

     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 4);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::array);
     BOOST_TEST_EQ(res->depth, 0u);
     BOOST_TEST_EQ(res->aggregate_size, 2u);
     BOOST_TEST(!p.done());

     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error_code{});
     BOOST_TEST_EQ(p.get_consumed(), 15);
     BOOST_TEST_EQ(res->data_type, boost::redis::resp3::type::blob_string);
     BOOST_TEST_EQ(res->depth, 1u);
     BOOST_TEST_EQ(res->aggregate_size, 0u);
     BOOST_TEST(!p.done());

     res = p.consume(data, ec);
     BOOST_TEST_EQ(ec, error::empty_field);
   }
}

void test_simple_error_adapter()
{
   { char const* msg = "-abcd\r\n";

     response<std::string> resp;

     auto adapter = boost_redis_adapt(resp);
     parser p;
     error_code ec;
     auto const res = parse(p, msg, adapter, ec);
     BOOST_TEST_EQ(ec, error_code());
     BOOST_TEST(res);
     BOOST_TEST(!std::get<0>(resp).has_value());
     BOOST_TEST(std::get<0>(resp).has_error());
     BOOST_TEST_EQ(std::get<0>(resp).error().data_type, resp3::type::simple_error);
     BOOST_TEST_EQ(std::get<0>(resp).error().diagnostic, "abcd");
   }

   { std::string_view const d1 = "-ab", d2 = "cd", d3 = "ef\r\n";

     response<std::string> resp;

     auto adapter = boost_redis_adapt(resp);
     parser p;
     error_code ec;
     bool res = false;

     res = parse(p, d1, adapter, ec);
     BOOST_TEST_EQ(ec, error_code());
     BOOST_TEST(!res);

     p.rewind();
     res = parse(p, d2, adapter, ec);
     BOOST_TEST_EQ(ec, error_code());
     BOOST_TEST(!res);

     p.rewind();
     res = parse(p, d3, adapter, ec);
     BOOST_TEST_EQ(ec, error_code());
     BOOST_TEST(res);

     BOOST_TEST(!std::get<0>(resp).has_value());
     BOOST_TEST(std::get<0>(resp).has_error());
     BOOST_TEST_EQ(std::get<0>(resp).error().data_type, resp3::type::simple_error);
     BOOST_TEST_EQ(std::get<0>(resp).error().diagnostic, "abcdef");
   }
}

}  // namespace

int main()
{
   // Simple data types
   test_parse_number();
   test_parse_boolean();
   test_parse_null();
   test_parse_simple_string();
   test_parse_big_number();
   test_parse_double();
   test_parse_blob_string();
   test_parse_streamed_string();
   test_parse_verbatim_string();

   // Aggregates
   test_parse_array();
   test_parse_set();
   test_parse_map();

   // Simple adapters
   test_simple_string_adapter();
   test_simple_error_adapter();

   // Aggregate adapters
   test_adapter_map_string_string();
   test_adapter_unordered_map_string_string();
   test_adapter_vector_string();
   test_adapter_vector_int();
   test_adapter_vector_pair_string();

   return boost::report_errors();
}
