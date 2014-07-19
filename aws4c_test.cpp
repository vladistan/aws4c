/**

*/

/*
 *
 * Copyright(c) 2009,  Vlad Korolev,  <vlad[@]v-lad.org >
 *
 * with contributions from Henry Nestler < Henry at BigFoot.de >
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution. The terms
 * are also available at http://www.gnu.org/licenses/lgpl-3.0.txt
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 */


#include <CppUTest/CommandLineTestRunner.h>
#include <CppUTest/JUnitTestOutput.h>
#include <stdio.h>
#include <string.h>
#include "aws4c.h"


TEST_GROUP(ChompTest)
{
};

TEST_GROUP(IsoDate)
{
};

TEST_GROUP(HttpDate)
{
};


TEST_GROUP(Base64)
{
};

TEST_GROUP(UrlEncode)
{
};


TEST(ChompTest, FirstTest)
{
   STRCMP_EQUAL("hello", "hello");
   LONGS_EQUAL(1, 1);
   CHECK(true);
}

extern "C" void __chomp ( char  * str );
extern "C" char * __aws_get_iso_date_t (time_t t);
extern "C" char * __aws_get_httpdate_t(time_t t);
extern "C" void __chomp ( char  * str );
extern "C" char *__b64_encode(const unsigned char *input, int length);
extern "C" void __aws_urlencode ( char * src, char * dest, int nDest );

TEST(UrlEncode,  UrlEncode_ShouldWorkDoNothingToSimpleString)
{
 char * input = "ABCD";
 char  out[25];

 __aws_urlencode(input, out, sizeof(out));

 STRCMP_EQUAL("ABCD", out);

}

TEST(UrlEncode,  UrlEncode_ShouldWorkEncodeSpecialCharsCorrectly)
{
 char * input = "ABCD/BB?";
 char  out[25];

 __aws_urlencode(input, out, sizeof(out));

 STRCMP_EQUAL("ABCD%2FBB%3F", out);

}


TEST(Base64,  Base64_ShouldEncodeSimpleStringRight)
{

 const unsigned char * input = (unsigned char *)"ABCD";
 char *out =  __b64_encode(input, 4);

 STRCMP_EQUAL("QUJDRA==", out);

}

TEST(Base64,  Base64_ShouldEncodeWorkWithEmptyStrings)
{

 const unsigned char * input = (unsigned char *)"";
 char *out =  __b64_encode(input, 1);

 STRCMP_EQUAL("AA==", out);

}

TEST(Base64,  Base64_ShouldEncodeIncludingTerminator)
{

 const unsigned char * input = (unsigned char *)"ABCD";
 char *out =  __b64_encode(input, 5);

 STRCMP_EQUAL("QUJDRAA=", out);

}


TEST(Base64,  Base64_ShouldEncodeIncludingNullInTheMiddle)
{

 const unsigned char * input = (unsigned char *)"AB\0CD";
 char *out =  __b64_encode(input, 6);

 STRCMP_EQUAL("QUIAQ0QA", out);

}


TEST(ChompTest, Chomp_ShouldHandleEmptyString)
{
   char * testStr = strdup("");
   __chomp(testStr);
   STRCMP_EQUAL("", testStr);
}

TEST(ChompTest, Chomp_ShouldWorkWithNormalString)
{
   char * testStr = strdup("Test\n");
   __chomp(testStr);
   STRCMP_EQUAL("Test", testStr);
}

TEST(ChompTest, Chomp_ShouldHandleCRandNL)
{
   char * testStr = strdup("Test\r\n");
   __chomp(testStr);
   STRCMP_EQUAL("Test", testStr);
}

TEST(ChompTest, Chomp_ShouldNotChokeOnSingleNL)
{
   char * testStr = strdup("\n");
   __chomp(testStr);
   STRCMP_EQUAL("", testStr);
}

TEST(ChompTest, Chomp_ShouldNotChangeStrsWithNoNL)
{
   char * testStr = "Hello";
   __chomp(testStr);
   STRCMP_EQUAL("Hello", testStr);
}

TEST(ChompTest, Chomp_ShouldIgnoreNLNotAtTheEnd)
{
   char * testStr = "He\nllo";
   __chomp(testStr);
   STRCMP_EQUAL("He\nllo", testStr);
}


TEST(IsoDate, IsoDateReturnsCorrectDate)
{
   char * testStr = __aws_get_iso_date_t(1312345678);
   STRCMP_EQUAL("2011-08-03T04:27:58Z", testStr);
}


TEST(HttpDate, HttpDateReturnsCorrectDate)
{
   char * testStr = __aws_get_httpdate_t(1312345678);
   STRCMP_EQUAL("Wed, 03 Aug 2011 04:27:58 +0000", testStr);
}


int main(int ac, char** av)
{
    return CommandLineTestRunner::RunAllTests(ac, av);
}
