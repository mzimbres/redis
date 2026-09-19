/* Copyright (c) 2018-2025 Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#include <boost/redis/adapter/adapt.hpp>
#include <boost/redis/adapter/any_adapter.hpp>
#include <boost/redis/detail/read_buffer.hpp>
#include <boost/redis/request.hpp>
#include <boost/redis/resp3/node.hpp>
#include <boost/redis/resp3/serialization.hpp>
#include <boost/redis/resp3/type.hpp>
#include <boost/redis/response.hpp>

#include <boost/core/lightweight_test.hpp>

#include <iostream>
#include <string>

using namespace boost::redis;
using adapter::adapt2;
using adapter::result;
using resp3::tree;
using resp3::detail::deserialize;
using resp3::node;
using resp3::node_view;
using resp3::to_string;
using boost::system::error_code;

namespace resp3 = boost::redis::resp3;

namespace {

#define RESP3_SET_PART1 "~6\r\n+orange\r"
#define RESP3_SET_PART2 "\n+apple\r\n+one"
#define RESP3_SET_PART3 "\r\n+two\r"
#define RESP3_SET_PART4 "\n+three\r\n+orange\r\n"
char const* resp3_set = RESP3_SET_PART1 RESP3_SET_PART2 RESP3_SET_PART3 RESP3_SET_PART4;

void test_low_level_sync_sans_io()
{
   try {
      result<std::set<std::string>> resp;

      error_code ec;
      deserialize(resp3_set, adapt2(resp), ec);
      BOOST_TEST_EQ(ec, error_code{});

      for (auto const& e : resp.value())
         std::cout << e << std::endl;

   } catch (std::exception const& e) {
      std::cerr << e.what() << std::endl;
      exit(EXIT_FAILURE);
   }
}

void test_issue_210_empty_set()
{
   try {
      result<std::tuple<
         result<int>,
         result<std::vector<std::string>>,
         result<std::string>,
         result<int>>>
         resp;

      char const* wire = "*4\r\n:1\r\n~0\r\n$25\r\nthis_should_not_be_in_set\r\n:2\r\n";

      error_code ec;
      deserialize(wire, adapt2(resp), ec);
      BOOST_TEST_EQ(ec, error_code{});

      BOOST_TEST_EQ(std::get<0>(resp.value()).value(), 1);
      BOOST_TEST(std::get<1>(resp.value()).value().empty());
      BOOST_TEST_EQ(std::get<2>(resp.value()).value(), "this_should_not_be_in_set");
      BOOST_TEST_EQ(std::get<3>(resp.value()).value(), 2);

   } catch (std::exception const& e) {
      std::cerr << e.what() << std::endl;
      exit(EXIT_FAILURE);
   }
}

void test_issue_210_non_empty_set_size_one()
{
   try {
      result<std::tuple<
         result<int>,
         result<std::vector<std::string>>,
         result<std::string>,
         result<int>>>
         resp;

      char const*
         wire = "*4\r\n:1\r\n~1\r\n$3\r\nfoo\r\n$25\r\nthis_should_not_be_in_set\r\n:2\r\n";

      error_code ec;
      deserialize(wire, adapt2(resp), ec);
      BOOST_TEST_EQ(ec, error_code{});

      BOOST_TEST_EQ(std::get<0>(resp.value()).value(), 1);
      BOOST_TEST_EQ(std::get<1>(resp.value()).value().size(), 1u);
      BOOST_TEST_EQ(std::get<1>(resp.value()).value().at(0), std::string{"foo"});
      BOOST_TEST_EQ(std::get<2>(resp.value()).value(), "this_should_not_be_in_set");
      BOOST_TEST_EQ(std::get<3>(resp.value()).value(), 2);

   } catch (std::exception const& e) {
      std::cerr << e.what() << std::endl;
      exit(EXIT_FAILURE);
   }
}

void test_issue_210_non_empty_set_size_two()
{
   try {
      result<std::tuple<
         result<int>,
         result<std::vector<std::string>>,
         result<std::string>,
         result<int>>>
         resp;

      char const* wire =
         "*4\r\n:1\r\n~2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n$25\r\nthis_should_not_be_in_set\r\n:2\r\n";

      error_code ec;
      deserialize(wire, adapt2(resp), ec);
      BOOST_TEST_EQ(ec, error_code{});

      BOOST_TEST_EQ(std::get<0>(resp.value()).value(), 1);
      BOOST_TEST_EQ(std::get<1>(resp.value()).value().at(0), std::string{"foo"});
      BOOST_TEST_EQ(std::get<1>(resp.value()).value().at(1), std::string{"bar"});
      BOOST_TEST_EQ(std::get<2>(resp.value()).value(), "this_should_not_be_in_set");

   } catch (std::exception const& e) {
      std::cerr << e.what() << std::endl;
      exit(EXIT_FAILURE);
   }
}

void test_issue_210_no_nested()
{
   try {
      result<std::tuple<result<int>, result<std::string>, result<std::string>, result<std::string>>>
         resp;

      char const*
         wire = "*4\r\n:1\r\n$3\r\nfoo\r\n$3\r\nbar\r\n$25\r\nthis_should_not_be_in_set\r\n";

      error_code ec;
      deserialize(wire, adapt2(resp), ec);
      BOOST_TEST_EQ(ec, error_code{});

      BOOST_TEST_EQ(std::get<0>(resp.value()).value(), 1);
      BOOST_TEST_EQ(std::get<1>(resp.value()).value(), std::string{"foo"});
      BOOST_TEST_EQ(std::get<2>(resp.value()).value(), std::string{"bar"});
      BOOST_TEST_EQ(std::get<3>(resp.value()).value(), "this_should_not_be_in_set");

   } catch (std::exception const& e) {
      std::cerr << e.what() << std::endl;
      exit(EXIT_FAILURE);
   }
}

void test_issue_233_array_with_null()
{
   try {
      result<std::vector<std::optional<std::string>>> resp;

      char const* wire = "*3\r\n+one\r\n_\r\n+two\r\n";

      error_code ec;
      deserialize(wire, adapt2(resp), ec);
      BOOST_TEST_EQ(ec, error_code{});

      BOOST_TEST_EQ(resp.value().at(0).value(), "one");
      BOOST_TEST(!resp.value().at(1).has_value());
      BOOST_TEST_EQ(resp.value().at(2).value(), "two");

   } catch (std::exception const& e) {
      std::cerr << e.what() << std::endl;
      exit(EXIT_FAILURE);
   }
}

void test_issue_233_optional_array_with_null()
{
   try {
      result<std::optional<std::vector<std::optional<std::string>>>> resp;

      char const* wire = "*3\r\n+one\r\n_\r\n+two\r\n";

      error_code ec;
      deserialize(wire, adapt2(resp), ec);
      BOOST_TEST_EQ(ec, error_code{});

      BOOST_TEST_EQ(resp.value().value().at(0).value(), "one");
      BOOST_TEST(!resp.value().value().at(1).has_value());
      BOOST_TEST_EQ(resp.value().value().at(2).value(), "two");

   } catch (std::exception const& e) {
      std::cerr << e.what() << std::endl;
      exit(EXIT_FAILURE);
   }
}

void test_check_counter_adapter()
{
   using boost::redis::any_adapter;
   using boost::redis::resp3::write;
   using boost::redis::resp3::parser;
   using boost::redis::resp3::node_view;
   using boost::system::error_code;

   int init = 0;
   int node = 0;
   int done = 0;

   auto counter_adapter = [&](any_adapter::parse_event ev, node_view const&, error_code&) mutable {
      switch (ev) {
         case any_adapter::parse_event::init: init++; break;
         case any_adapter::parse_event::node: node++; break;
         case any_adapter::parse_event::done: done++; break;
      }
   };

   any_adapter wrapped{any_adapter::impl_t{counter_adapter}};

   error_code ec;
   parser p;

   auto const ret1 = write(p, RESP3_SET_PART1, wrapped, ec);
   auto const ret2 = write(p, RESP3_SET_PART1 RESP3_SET_PART2, wrapped, ec);
   auto const ret3 = write(p, RESP3_SET_PART1 RESP3_SET_PART2 RESP3_SET_PART3, wrapped, ec);
   auto const ret4 = write(
      p,
      RESP3_SET_PART1 RESP3_SET_PART2 RESP3_SET_PART3 RESP3_SET_PART4,
      wrapped,
      ec);

   BOOST_TEST(!ec && ret1 == 0);
   BOOST_TEST(!ec && ret2 == 0);
   BOOST_TEST(!ec && ret3 == 0);
   BOOST_TEST(!ec && ret4 != 0);

   BOOST_TEST_EQ(init, 1);
   BOOST_TEST_EQ(node, 7);
   BOOST_TEST_EQ(done, 1);
}

void test_parse_int()
{
   using boost::redis::resp3::parser;
   using boost::redis::error;
   using boost::system::error_code;

   { std::string_view const data = ":42\r\n";

     parser p;
     error_code ec;
     auto const res = p.write(data, ec);
     BOOST_TEST(!ec);
     BOOST_TEST_EQ(res.consumed, 5);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::number);
     BOOST_TEST_EQ(res.node.depth, 0u);
     BOOST_TEST_EQ(res.node.aggregate_size, 1u);
     BOOST_TEST_EQ(res.node.value, "42\r\n");
     BOOST_TEST(p.done());
   }

   { std::string_view const data = ":-42\r\n";

     parser p;
     error_code ec;
     auto const res = p.write(data, ec);
     BOOST_TEST(!ec);
     BOOST_TEST_EQ(res.consumed, 6);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::number);
     BOOST_TEST_EQ(res.node.depth, 0u);
     BOOST_TEST_EQ(res.node.aggregate_size, 1u);
     BOOST_TEST_EQ(res.node.value, "-42\r\n");
     BOOST_TEST(p.done());
   }

   { std::string_view const data1 = ":1234", data2 = ":123456789\r\n";

     parser p;
     error_code ec;
     parser::result res;

     // Part 1
     res = p.write(data1, ec);
     BOOST_TEST(!ec);
     BOOST_TEST_EQ(res.consumed, 5);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::number);
     BOOST_TEST_EQ(res.node.depth, 0u);
     BOOST_TEST_EQ(res.node.aggregate_size, 1u);
     BOOST_TEST_EQ(res.node.value, "1234");
     BOOST_TEST(!p.done());

     // Part 2
     res = p.write(data2, ec);
     BOOST_TEST_EQ(res.consumed, 12);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::number);
     BOOST_TEST_EQ(res.node.depth, 0u);
     BOOST_TEST_EQ(res.node.aggregate_size, 1u);
     BOOST_TEST_EQ(res.node.value, "56789\r\n");
     BOOST_TEST(p.done());
   }

   { std::string_view const data1 = ":1234", data2 = "56789\r\n";

     parser p;
     error_code ec;
     parser::result res;

     // Part 1
     res = p.write(data1, ec);
     BOOST_TEST(!ec);
     BOOST_TEST_EQ(res.consumed, 5);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::number);
     BOOST_TEST_EQ(res.node.depth, 0u);
     BOOST_TEST_EQ(res.node.aggregate_size, 1u);
     BOOST_TEST_EQ(res.node.value, "1234");
     BOOST_TEST(!p.done());

     // Part 2
     p.rewind();
     res = p.write(data2, ec);
     BOOST_TEST_EQ(res.consumed, 7);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::number);
     BOOST_TEST_EQ(res.node.depth, 0u);
     BOOST_TEST_EQ(res.node.aggregate_size, 1u);
     BOOST_TEST_EQ(res.node.value, "56789\r\n");
     BOOST_TEST(p.done());
   }
}

void test_simple_string_adapter()
{
   using boost::redis::resp3::parser;
   using boost::redis::error;
   using boost::system::error_code;

   { std::string_view const data = "+abc\r\n";

     result<std::string> resp;
     auto adapter = adapt2(resp);
     parser p;
     error_code ec;
     write(p, data, adapter, ec);
     BOOST_TEST_EQ(ec, error_code());
     BOOST_TEST_EQ(resp.value(), "abc");
   }
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

     res = p.write(data, ec);
     BOOST_TEST(!ec);
     BOOST_TEST_EQ(res.consumed, 6);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res.node.depth, 0u);
     BOOST_TEST_EQ(res.node.aggregate_size, 1u);
     BOOST_TEST_EQ(res.node.value, "abcd\n");
     BOOST_TEST(!p.done());
   }

   { std::string_view const data = "+ab\rd\nefg\r\n";

     parser p;
     error_code ec;
     parser::result res;

     res = p.write(data, ec);
     BOOST_TEST(!ec);
     BOOST_TEST_EQ(res.consumed, 11);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res.node.depth, 0u);
     BOOST_TEST_EQ(res.node.aggregate_size, 1u);
     BOOST_TEST_EQ(res.node.value, "ab\rd\nefg\r\n");
     BOOST_TEST(p.done());
   }

   { std::string_view const data1 = "+abcdefg\r", data2 = "\n";

     parser p;
     error_code ec;
     parser::result res;

     res = p.write(data1, ec);
     BOOST_TEST(!ec);
     BOOST_TEST_EQ(res.consumed, 9);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res.node.depth, 0u);
     BOOST_TEST_EQ(res.node.aggregate_size, 1u);
     BOOST_TEST_EQ(res.node.value, "abcdefg\r");
     BOOST_TEST(!p.done());

     p.rewind();
     res = p.write(data2, ec);
     BOOST_TEST(!ec);
     BOOST_TEST_EQ(res.consumed, 1);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res.node.depth, 0u);
     BOOST_TEST_EQ(res.node.aggregate_size, 1u);
     BOOST_TEST_EQ(res.node.value, "\n");
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
     res = p.write(data, ec);
     BOOST_TEST(!ec);
     BOOST_TEST_EQ(res.consumed, 4);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::set);
     BOOST_TEST_EQ(res.node.depth, 0u);
     BOOST_TEST_EQ(res.node.aggregate_size, 2u);
     BOOST_TEST(!p.done());

     // Second node
     res = p.write(data, ec);
     BOOST_TEST(!ec);
     BOOST_TEST_EQ(res.consumed, 10);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res.node.depth, 1u);
     BOOST_TEST_EQ(res.node.value, "one\r\n");
     BOOST_TEST_EQ(res.node.aggregate_size, 1u);
     BOOST_TEST(!p.done());

     // Third node
     res = p.write(data, ec);
     BOOST_TEST(!ec);
     BOOST_TEST_EQ(res.consumed, 15);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::number);
     BOOST_TEST_EQ(res.node.depth, 1u);
     BOOST_TEST_EQ(res.node.value, "42\r\n");
     BOOST_TEST_EQ(res.node.aggregate_size, 1u);
     BOOST_TEST(p.done());
   }
}

void test_parse_map()
{
   using boost::redis::resp3::parser;
   using boost::redis::error;
   using boost::system::error_code;

   { std::string_view const data = "%2\r\n+key1\r\n+value1\r\n+key2\r\n+value2\r\n";

     parser p;
     error_code ec;
     parser::result res;

     // First node
     res = p.write(data, ec);
     BOOST_TEST(!ec);
     BOOST_TEST_EQ(p.get_consumed(), 4);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::map);
     BOOST_TEST_EQ(res.node.depth, 0u);
     BOOST_TEST_EQ(res.node.aggregate_size, 2u);
     BOOST_TEST(!p.done());

     // Second node
     res = p.write(data, ec);
     BOOST_TEST(!ec);
     BOOST_TEST_EQ(p.get_consumed(), 11);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res.node.depth, 1u);
     BOOST_TEST_EQ(res.node.value, "key1\r\n");
     BOOST_TEST_EQ(res.node.aggregate_size, 1u);
     BOOST_TEST(!p.done());

     // Third node
     res = p.write(data, ec);
     BOOST_TEST(!ec);
     BOOST_TEST_EQ(p.get_consumed(), 20);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res.node.depth, 1u);
     BOOST_TEST_EQ(res.node.value, "value1\r\n");
     BOOST_TEST_EQ(res.node.aggregate_size, 1u);
     BOOST_TEST(!p.done());

     // Fourth node
     res = p.write(data, ec);
     BOOST_TEST(!ec);
     BOOST_TEST_EQ(p.get_consumed(), 27);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res.node.depth, 1u);
     BOOST_TEST_EQ(res.node.value, "key2\r\n");
     BOOST_TEST_EQ(res.node.aggregate_size, 1u);
     BOOST_TEST(!p.done());

     // Fifth node
     res = p.write(data, ec);
     BOOST_TEST(!ec);
     BOOST_TEST_EQ(p.get_consumed(), 36);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::simple_string);
     BOOST_TEST_EQ(res.node.depth, 1u);
     BOOST_TEST_EQ(res.node.value, "value2\r\n");
     BOOST_TEST_EQ(res.node.aggregate_size, 1u);
     BOOST_TEST(p.done());
   }
}

void test_parse_uint()
{
   using boost::redis::resp3::detail::add_digit;
   using boost::redis::error;
   using boost::system::error_code;

   {
      error_code ec;
      std::size_t result = 0;

      add_digit(result, '1', ec);
      BOOST_TEST(!ec);
      BOOST_TEST_EQ(result, 1);
      add_digit(result, '2', ec);
      BOOST_TEST(!ec);
      BOOST_TEST_EQ(result, 12);
      add_digit(result, '3', ec);
      BOOST_TEST(!ec);
      BOOST_TEST_EQ(result, 123);
      add_digit(result, 'a', ec);
      BOOST_TEST_EQ(ec, error::not_a_number);
      BOOST_TEST_EQ(result, 123);
   }
}

void test_parse_streamed_string()
{
   using boost::redis::resp3::parser;
   using boost::redis::error;
   using boost::system::error_code;

   { std::string_view const data = "$?\r\n;5\r\n12345\r\n";

     parser p;
     error_code ec;
     parser::result res;

     res = p.write(data, ec);
     BOOST_TEST(!ec);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::streamed_string);
     BOOST_TEST_EQ(res.node.depth, 0u);
     BOOST_TEST_EQ(res.node.aggregate_size, 1u);
     BOOST_TEST_EQ(res.node.value, "");
     BOOST_TEST(p.done());

     p.reset();
     res = p.write(data, ec);
     BOOST_TEST(!ec);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::streamed_string_part);
     BOOST_TEST_EQ(res.node.depth, 0u);
     BOOST_TEST_EQ(res.node.aggregate_size, 1u);
     BOOST_TEST_EQ(res.node.value, "12345");
     BOOST_TEST(p.done());

     p.reset();
     res = p.write(data, ec);
     BOOST_TEST(!ec);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::streamed_string_part);
     BOOST_TEST_EQ(res.node.depth, 0u);
     BOOST_TEST_EQ(res.node.aggregate_size, 1u);
     BOOST_TEST_EQ(res.node.value, "67890");
     BOOST_TEST(p.done());
   }

}

void test_parse_blob_string()
{
   using boost::redis::resp3::parser;
   using boost::redis::error;
   using boost::system::error_code;

   { std::string_view const data = "$11\r\nboost.redis\r\n";

     parser p;
     error_code ec;
     parser::result res;

     res = p.write(data, ec);
     BOOST_TEST(!ec);
     BOOST_TEST_EQ(res.consumed, 18);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::blob_string);
     BOOST_TEST_EQ(res.node.depth, 0u);
     BOOST_TEST_EQ(res.node.aggregate_size, 1u);
     BOOST_TEST_EQ(res.node.value, "boost.redis\r\n");
     BOOST_TEST(p.done());
   }

   { std::string_view const d1 = "$11\r\nboos", d2 = "t.redi", d3 = "s\r\n";

     parser p;
     error_code ec;
     parser::result res;

     res = p.write(d1, ec);
     BOOST_TEST(!ec);
     BOOST_TEST_EQ(res.consumed, 9);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::blob_string);
     BOOST_TEST_EQ(res.node.depth, 0u);
     BOOST_TEST_EQ(res.node.aggregate_size, 1u);
     BOOST_TEST_EQ(res.node.value, "boos");
     BOOST_TEST(!p.done());

     p.rewind();
     res = p.write(d2, ec);
     BOOST_TEST(!ec);
     BOOST_TEST_EQ(res.consumed, 6);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::blob_string);
     BOOST_TEST_EQ(res.node.depth, 0u);
     BOOST_TEST_EQ(res.node.aggregate_size, 1u);
     BOOST_TEST_EQ(res.node.value, "t.redi");
     BOOST_TEST(!p.done());

     p.rewind();
     res = p.write(d3, ec);
     BOOST_TEST(!ec);
     BOOST_TEST_EQ(res.consumed, 3);
     BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::blob_string);
     BOOST_TEST_EQ(res.node.depth, 0u);
     BOOST_TEST_EQ(res.node.aggregate_size, 1u);
     BOOST_TEST_EQ(res.node.value, "s\r\n");
     BOOST_TEST(p.done());
   }
}

}  // namespace

int main()
{
   test_parse_streamed_string();
   test_parse_blob_string();
   test_parse_simple_string();
   test_parse_int();
   test_parse_set();
   test_parse_map();
   test_parse_uint();
   test_simple_string_adapter();

   //test_low_level_sync_sans_io();
   //test_issue_210_empty_set();
   //test_issue_210_non_empty_set_size_one();
   //test_issue_210_non_empty_set_size_two();
   //test_issue_210_no_nested();
   //test_issue_233_array_with_null();
   //test_issue_233_optional_array_with_null();
   //test_check_counter_adapter();

   return boost::report_errors();
}
