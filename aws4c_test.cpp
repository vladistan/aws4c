#include <CppUTest/CommandLineTestRunner.h>
#include <CppUTest/JUnitTestOutput.h>
#include <stdio.h>
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
   char * testStr = "";    

   __chomp(testStr);

   STRCMP_EQUAL("",testStr);

}

int main(int ac, char** av)
{
    return CommandLineTestRunner::RunAllTests(ac, av);
}
