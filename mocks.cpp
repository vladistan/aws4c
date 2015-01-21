#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "test_mocks.h"
#include "CppUTest/TestHarness_c.h"
#include "CppUTestExt/MockSupport.h"
#include <CppUTestExt/MockSupport_c.h>


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