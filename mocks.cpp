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


extern "C" CURLcode curl_easy_setopt(CURL *curl, CURLoption option, ...)
{
    va_list vl;
    va_start(vl,option);

    void * arg = va_arg(vl, void*);

    mock().actualCall("curl_easy_setopt")
            .withParameter("curl", (void*)curl)
            .withParameter("option", option)
            .withParameter("arg", (const void *)arg);


    va_end(vl);

    return CURLE_OK;
}

extern "C" CURLcode curl_easy_perform(CURL *handle)
{
    mock().actualCall("curl_easy_perform")
            .withParameter("curl", (void*)handle);

    return CURLE_OK;

}


extern "C" void curl_slist_free_all(struct curl_slist *list)
{
    mock().actualCall("curl_slist_free_all")
            .withParameter("list", (void*)list);

}