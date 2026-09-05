/* Copyright (c) Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#include <boost/redis/resp3/detail/simple_parser.hpp>

#include <boost/core/lightweight_test.hpp>

#include <iostream>
#include <string>

using boost::redis::error;
using boost::redis::resp3::detail::simple_parser;
using boost::system::error_code;

namespace resp3 = boost::redis::resp3;

namespace {

void test_parse_null()
{
   //{ std::string_view const data = "_\r\n";

   //  header_parser p;
   //  error_code ec;
   //  auto const res = p.write(data, ec);
   //  BOOST_TEST(!ec);
   //  BOOST_TEST_EQ(res.node.data_type, boost::redis::resp3::type::null);
   //  BOOST_TEST_EQ(res.node.depth, 0u);
   //  BOOST_TEST(p.done());
   //}
}

}  // namespace

int main()
{
   test_parse_null();

   return boost::report_errors();
}
