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


TEST(ChompTest, FirstTest)
{
   STRCMP_EQUAL("hello", "hello");
   LONGS_EQUAL(1, 1);
   CHECK(true);
}

extern "C" void __chomp ( char  * str );
extern "C" char * __aws_get_iso_date_t (time_t t);


TEST(ChompTest, Chomp_ShouldHandleEmptyString)
{
   char * testStr = strdup("");    
   __chomp(testStr);
   STRCMP_EQUAL("",testStr);
}

TEST(ChompTest, Chomp_ShouldWorkWithNormalString)
{
   char * testStr = strdup("Test\n");    
   __chomp(testStr);
   STRCMP_EQUAL("Test",testStr);
}

TEST(ChompTest, Chomp_ShouldHandleCRandNL)
{
   char * testStr = strdup("Test\r\n");    
   __chomp(testStr);
   STRCMP_EQUAL("Test",testStr);
}

TEST(ChompTest, Chomp_ShouldNotChokeOnSingleNL)
{
   char * testStr = strdup("\n");    
   __chomp(testStr);
   STRCMP_EQUAL("",testStr);
}

TEST(ChompTest, Chomp_ShouldNotChangeStrsWithNoNL)
{
   char * testStr = "Hello";    
   __chomp(testStr);
   STRCMP_EQUAL("Hello",testStr);
}

TEST(ChompTest, Chomp_ShouldIgnoreNLNotAtTheEnd)
{
   char * testStr = "He\nllo";    
   __chomp(testStr);
   STRCMP_EQUAL("He\nllo",testStr);
}


TEST(IsoDate, IsoDateReturnsCorrectDate)
{
   char * testStr = __aws_get_iso_date_t(1312345678);
   STRCMP_EQUAL("2011-08-03T04:27:58Z",testStr);
}




int main(int ac, char** av)
{
    return CommandLineTestRunner::RunAllTests(ac, av);
}
