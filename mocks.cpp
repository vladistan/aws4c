#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "test_mocks.h"
#include "CppUTest/TestHarness_c.h"
#include "CppUTestExt/MockSupport.h"
#include <CppUTestExt/MockSupport_c.h>
#include <curl/curl.h>


extern "C" int fprintf(FILE * __restrict out, const char * __restrict fmt, ...)
{

      mock().actualCall("fprintf")
        .withParameter("stream", out)
        .withParameter("fmt", fmt);

     return 1;
}


extern "C" int vfprintf(FILE * __restrict out, const char * __restrict fmt , va_list args)
{
    mock().actualCall("vfprintf")
        .withParameter("stream", out)
        .withParameter("fmt", fmt);

    return 1;
}

extern "C" time_t time(time_t *tloc)
{

    return 1312345678;

}


CURL *curl_easy_init( )
{
    mock().actualCall("curl_easy_init");
    return NULL;
}


CURLcode curl_global_init(long flags)
{
    mock().actualCall("curl_init")
            .withParameter("flags", (int)flags);
    return CURLE_OK;
}


char * TEST_HOME="./test_data/";
extern "C" char * GetEnv(const char *name)
{
    return TEST_HOME;
}