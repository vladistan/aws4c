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
#include <CppUTestExt/MockSupport.h>
#include <stdio.h>
#include <string.h>
#include <curl/curl.h>
extern "C" {
#include "aws4c.h"
extern "C" int debug;
}

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

TEST_GROUP(Debug)
{
    int saveDebug;
    void setup()
    {
        saveDebug = debug;
        debug = 1;
    }

    void teardown()
    {
        mock().checkExpectations();
        mock().removeAllComparators();
        mock().clear();
        debug = saveDebug;
    }

};

TEST_GROUP(IOBuf)
{

};


TEST_GROUP(Config)
{

};

TEST_GROUP(Header)
{

};


TEST_GROUP(AwsSign)
{

        void setup()
        {
            aws_set_key("AAAABBBCCCCDDDD");
        }

        void teardown()
        {
            mock().checkExpectations();
            mock().removeAllComparators();
            mock().clear();
            aws_set_key(NULL);
        }
};


TEST_GROUP(SqsSign)
{
        void setup()
        {
            aws_set_key("AAAABBBCCCCDDDD");
        }

        void teardown()
        {
            aws_set_key(NULL);
        }

};

TEST_GROUP(SQSRequest)
{
        void setup()
        {
        }

        void teardown()
        {
            mock().checkExpectations();
            mock().removeAllComparators();
            mock().clear();
        }

};

TEST_GROUP(Init)
{
        void setup()
        {
            debug = 1;
        }

        void teardown()
        {
            mock().checkExpectations();
            mock().removeAllComparators();
            mock().clear();
        }
};



TEST(Init, Init)
{

    mock().expectOneCall("curl_init").withIntParameter("flags", CURL_GLOBAL_ALL);

    aws_init();

}



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
extern "C" void __debug ( char *fmt, ... );
extern "C" char * __aws_get_iso_date ();
extern "C" char * __aws_get_httpdate ();
extern "C" FILE * __aws_getcfg ();
extern "C" size_t header ( void * ptr, size_t size, size_t nmemb, void * stream );
extern "C" size_t writefunc ( void * ptr, size_t size, size_t nmemb, void * stream );
extern "C" size_t readfunc ( void * ptr, size_t size, size_t nmemb, void * stream );
extern "C" char* __aws_sign ( char * const str );
extern "C" char * GetStringToSign ( char * resource,  int resSize, char ** date,  char * const method, char * const bucket, char * const file );
extern "C" int SQSRequest ( IOBuf *b, char * verb, char * const url );
extern "C" char * SQSSign ( char * str );

TEST(AwsSign, SimpleSign)
{

    char * sign;


    sign = __aws_sign("Test String");

    STRCMP_EQUAL( "MGZZI0fWK24JzbB7JRfFh6oDtPI=", sign  );

}

TEST(AwsSign, GetStringToSign)
{

    char * sign;

    char  resource [1024];
    char * date = NULL;

    sign = GetStringToSign(resource, sizeof(resource), &date, "GET", "mybucket", "myFile.tgz");

    STRCMP_EQUAL( "urB1lRwU2p0j0MWZIEzRIcFDFy0=", sign  );
    STRCMP_EQUAL( "Wed, 03 Aug 2011 04:27:58 +0000", date  );
    STRCMP_EQUAL( "mybucket/myFile.tgz", resource  );

}


TEST(AwsSign, GetStringToSignWithACL)
{

    char * sign;

    char  resource [1024];
    char * date = NULL;

    s3_set_acl("ACL_1");
    sign = GetStringToSign(resource, sizeof(resource),&date,"GET","mybucket","myFile.tgz");

    STRCMP_EQUAL( "8AsjYPW+6FR8DzefCTK/yr35poY=", sign  );
    STRCMP_EQUAL( "Wed, 03 Aug 2011 04:27:58 +0000", date  );
    STRCMP_EQUAL( "mybucket/myFile.tgz", resource  );

    s3_set_acl(NULL);

}


TEST(AwsSign, GetStringToSignWithMime)
{

    char * sign;

    char  resource [1024];
    char * date = NULL;

    s3_set_mime("image/png");
    sign = GetStringToSign(resource, sizeof(resource),&date,"GET","mybucket","myFile.tgz");

    STRCMP_EQUAL( "kyXV98Mnu20Jf+6kHppC1sX5oDI=", sign  );
    STRCMP_EQUAL( "Wed, 03 Aug 2011 04:27:58 +0000", date  );
    STRCMP_EQUAL( "mybucket/myFile.tgz", resource  );

    s3_set_mime(NULL);

}



TEST(Header, ResultCode)
{

    char  inp[] = "HTTP/1.1 301 Moved Permanently";
    IOBuf * b = aws_iobuf_new();

    header(inp, sizeof(inp), 1, b);

    STRCMP_EQUAL( "301 Moved Permanently", b -> result  );
    LONGS_EQUAL ( b -> code, 301 );
}

TEST(Header, ETag)
{

    char  inp[] = "ETag: 2334022299449";
    IOBuf * b = aws_iobuf_new();

    header(inp, sizeof(inp), 1, b);

    STRCMP_EQUAL( "2334022299449", b -> eTag  );

}

TEST(Header, LastModified)
{

    char  inp[] = "Last-Modified: Wed, 21 Jan 2015 22:57:47 GMT\n";
    IOBuf * b = aws_iobuf_new();

    header(inp, sizeof(inp), 1, b);

    STRCMP_EQUAL( "Wed, 21 Jan 2015 22:57:47 GMT", b -> lastMod  );

}

TEST(Header, ContentLength)
{

    char  inp[] = "Content-Length: 22233\n";
    IOBuf * b = aws_iobuf_new();

    header(inp, sizeof(inp), 1, b);

    LONGS_EQUAL ( b -> contentLen, 22233 );

}

TEST(Config, GetCFG)
{

    FILE * cfgFile = __aws_getcfg();

    CHECK( cfgFile != NULL );


}




TEST(IOBuf, IOBufInitializedCorrectly )
{
   IOBuf * b = aws_iobuf_new();

   POINTERS_EQUAL(b->first, NULL);
   POINTERS_EQUAL(b->current, NULL);
   POINTERS_EQUAL(b->pos, NULL);

}

TEST(IOBuf, IOBufAppendSingleLine )
{
    IOBuf * b = aws_iobuf_new();

    aws_iobuf_append(b, "Hello\n",  6 );

    CHECK ( b -> first != NULL );
    POINTERS_EQUAL ( b-> first,  b-> current );
    CHECK ( *b -> pos == 'H');
    CHECK ( b -> current -> buf[6] == 0 );
    POINTERS_EQUAL ( b-> first -> next , NULL );

}

TEST(IOBuf, IOBufAppendTwoLines )
{
    IOBuf * b = aws_iobuf_new();

    aws_iobuf_append(b, "Hello\n",  6 );
    aws_iobuf_append(b, "World\n",  6 );

    CHECK ( b -> first != NULL );
    CHECK ( b -> first -> next != NULL );
    POINTERS_EQUAL ( b-> first,  b-> current );
    CHECK ( *b -> pos == 'H');
    CHECK ( b -> current -> buf[6] == 0 );
    POINTERS_EQUAL ( b-> first -> next -> next , NULL );

}


TEST(IOBuf, IOBufReadSingleLine )
{
    IOBuf * b = aws_iobuf_new();

    aws_iobuf_append(b, "Hello\n",  6 );
    aws_iobuf_append(b, "World\n",  6 );

    char Str[90];

    aws_iobuf_getline(b, Str, 90 );

    STRCMP_EQUAL( Str, "Hello\n"  );

}

TEST(IOBuf, IOBufReadSingleLineFromTwoBlocks )
{
    IOBuf * b = aws_iobuf_new();

    aws_iobuf_append(b, "Hello",  6 );
    aws_iobuf_append(b, "World\n22",  6 );

    char Str[90];

    aws_iobuf_getline(b, Str, 90 );

    STRCMP_EQUAL( Str, "HelloWorld\n"  );

}


TEST(SQSRequest, Simple)
{

    IOBuf * bf = aws_iobuf_new();
    aws_iobuf_append(bf, "Hello\n",  6 );

    const void * url = "http://bob";

    mock().expectOneCall("curl_easy_init");
    mock().expectOneCall("curl_easy_setopt").withParameter("curl", (void*)NULL)
                                            .withParameter("option", CURLOPT_URL)
                                            .withParameter("arg", url);

    mock().expectOneCall("curl_easy_setopt").withParameter("curl", (void*)NULL)
                                            .withParameter("option", CURLOPT_HEADERDATA)
                                            .withParameter("arg", (const void *) bf);

    mock().expectOneCall("curl_easy_setopt").withParameter("curl", (void*)NULL)
                                            .withParameter("option", CURLOPT_VERBOSE)
                                            .withParameter("arg", (const void *) 0);


    mock().expectOneCall("curl_easy_setopt").withParameter("curl", (void*)NULL)
                                            .withParameter("option", CURLOPT_INFILESIZE)
                                            .withParameter("arg", (const void *) 6);



    mock().expectOneCall("curl_easy_setopt").withParameter("curl", (void*)NULL)
                                            .withParameter("option", CURLOPT_POST)
                                            .withParameter("arg", (const void *) 1);

    mock().expectOneCall("curl_easy_setopt").withParameter("curl", (void*)NULL)
                                            .withParameter("option", CURLOPT_POSTFIELDSIZE)
                                            .withParameter("arg", (const void *) 0);

    mock().expectOneCall("curl_easy_setopt").withParameter("curl", (void*)NULL)
                                            .withParameter("option", CURLOPT_HEADERFUNCTION)
                                            .withParameter("arg", (const void *) header);


    mock().expectOneCall("curl_easy_setopt").withParameter("curl", (void*)NULL)
                                            .withParameter("option", CURLOPT_WRITEFUNCTION)
                                            .withParameter("arg", (const void *) writefunc);


    mock().expectOneCall("curl_easy_setopt").withParameter("curl", (void*)NULL)
                                            .withParameter("option", CURLOPT_WRITEDATA)
                                            .withParameter("arg", (const void *) bf);


    mock().expectOneCall("curl_easy_setopt").withParameter("curl", (void*)NULL)
                                            .withParameter("option", CURLOPT_READFUNCTION)
                                            .withParameter("arg", (const void *) readfunc);


    mock().expectOneCall("curl_easy_setopt").withParameter("curl", (void*)NULL)
                                            .withParameter("option", CURLOPT_READDATA)
                                            .withParameter("arg", (const void *) bf);


    mock().expectOneCall("curl_easy_perform").withParameter("curl", (void*)NULL);

    mock().expectOneCall("curl_slist_free_all").withParameter("list", (void*)NULL);

    SQSRequest(bf,"POST", (char * const) url);



}


TEST(SqsSign, Simple)
{
    char * rv =   SQSSign("Bob the operator");

    STRCMP_EQUAL("3EjYzPkAdleCOKOgCv%2Fzsq1FuG8%3D", rv);

}


TEST(Debug,TestSimple)
{

    mock().expectOneCall("fprintf").withParameter("stream", stderr).withParameter("fmt", "DBG: ");
    mock().expectOneCall("vfprintf").withParameter("stream", stderr).withParameter("fmt", "Hello Test %d");
    mock().expectOneCall("fprintf").withParameter("stream", stderr).withParameter("fmt", "\n");


    __debug("Hello Test %d", 19 );

}

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


TEST(IsoDate, IsoDateReturnsCorrectDateUsingMock )
{
    char * testStr = __aws_get_iso_date();
    STRCMP_EQUAL("2011-08-03T04:27:58Z", testStr);
}


TEST(HttpDate, HttpDateReturnsCorrectDate)
{
   char * testStr = __aws_get_httpdate_t(1312345678);
   STRCMP_EQUAL("Wed, 03 Aug 2011 04:27:58 +0000", testStr);
}

TEST(HttpDate, HttpDateReturnsCorrectDate2)
{
    char * testStr = __aws_get_httpdate();
    STRCMP_EQUAL("Wed, 03 Aug 2011 04:27:58 +0000", testStr);
}




int main(int ac, char** av)
{
    return CommandLineTestRunner::RunAllTests(ac, av);
}
