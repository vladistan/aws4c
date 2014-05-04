#include <CppUTest/CommandLineTestRunner.h>
#include <CppUTest/JUnitTestOutput.h>
#include <stdio.h>
#include <string.h>
#include "aws4c.h"


TEST_GROUP(FirstTestGroup)
{
};


TEST(FirstTestGroup, FirstTest)
{
   STRCMP_EQUAL("hello", "hello");
   LONGS_EQUAL(1, 1);
   CHECK(true);
}

extern STATIC "C" void __chomp ( char  * str );

TEST(FirstTestGroup, Chomp_ShouldHandleEmptyString)
{
   char * testStr = strdup("");    
   __chomp(testStr);
   STRCMP_EQUAL("",testStr);
}

TEST(FirstTestGroup, Chomp_ShouldWorkWithNormalString)
{
   char * testStr = strdup("Test\n");    
   __chomp(testStr);
   STRCMP_EQUAL("Test",testStr);
}

TEST(FirstTestGroup, Chomp_ShouldHandleCRandNL)
{
   char * testStr = strdup("Test\r\n");    
   __chomp(testStr);
   STRCMP_EQUAL("Test",testStr);
}

TEST(FirstTestGroup, Chomp_ShouldNotChokeOnSingleNL)
{
   char * testStr = strdup("\n");    
   __chomp(testStr);
   STRCMP_EQUAL("",testStr);
}

TEST(FirstTestGroup, Chomp_ShouldNotChangeStrsWithNoNL)
{
   char * testStr = "Hello";    
   __chomp(testStr);
   STRCMP_EQUAL("Hello",testStr);
}

TEST(FirstTestGroup, Chomp_ShouldIgnoreNLNotAtTheEnd)
{
   char * testStr = "He\nllo";    
   __chomp(testStr);
   STRCMP_EQUAL("He\nllo",testStr);
}





int main(int ac, char** av)
{
    return CommandLineTestRunner::RunAllTests(ac, av);
}
