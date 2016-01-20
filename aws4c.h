#ifndef __AWS4C_H__
#define __AWS4C_H__


#include <curl/curl.h>


#  ifdef __cplusplus
extern "C" {
#  endif

/*
 *
 * Copyright(c) 2009,  Vlad Korolev,  <vlad[@]v-lad.org >
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


// For functions that return curl status, e.g. s3_get(), s3_put(),
// s3_post(), etc ...  this assures that the curl request was successful.
//
// WARNING: Don't confuse CURL "success" with "everything is okay".  A CURL
//          request may "fail", but still return CURLE_OK.  For example, a
//          HEAD request for a non-existent object may have an HTTP return
//          code of "404 Not Found", but return-value of CURLE_OK.  This
//          means "I successfully talked with the server and found that
//          your file doesn't exist."
//
// If you want to assure that "everything is okay", try something like
// this:
//
//     AWS4C_CHECK( s3_head( my_iobuf, obj_name) );
//     AWS4C_CHECK_OK( my_iobuf );



#define AWS4C_CHECK( FUNCALL )                                          \
   do {                                                                 \
      CURLcode rc = (FUNCALL);                                          \
      if ( rc != CURLE_OK ) {                                           \
         fprintf(stderr, "libcurl call failed at %s, line %d\nERROR: %s\n", \
                 __FILE__, __LINE__, curl_easy_strerror(rc));           \
         exit(1);                                                       \
      }                                                                 \
   } while (0)

// like AWS_CHECK(), but calls return instead of exit()
#define AWS4C_CHECK1( FUNCALL )                                         \
   do {                                                                 \
      CURLcode rc = (FUNCALL);                                          \
      if ( rc != CURLE_OK ) {                                           \
         fprintf(stderr, "libcurl call failed at %s, line %d\nERROR: %s\n", \
                 __FILE__, __LINE__, curl_easy_strerror(rc));           \
         return(rc);  /* guranteed non-zero */                          \
      }                                                                 \
   } while (0)

#define AWS4C_OK( IOBUF )    ( (IOBUF)->code == 200 )

#define AWS4C_CHECK_OK( IOBUF )                                         \
   do {                                                                 \
      if ( (IOBUF->code != 200 ) ) {                                    \
         fprintf(stderr, "CURL call failed at %s, line %d\nERROR: %d '%s'\n", \
                 __FILE__, __LINE__, (IOBUF)->code, (IOBUF)->result);   \
         exit(1);                                                       \
      }                                                                 \
   } while (0)

// like AWS_CHECK_OK(), but calls return, instead of exit()
#define AWS4C_CHECK_OK1( IOBUF )                                         \
   do {                                                                 \
      if ( (IOBUF->code != 200 ) ) {                                    \
         fprintf(stderr, "CURL call failed at %s, line %d\nERROR: %d '%s'\n", \
                 __FILE__, __LINE__, (IOBUF)->code, (IOBUF)->result);   \
         return(1);                                                       \
      }                                                                 \
   } while (0)


// ---------------------------------------------------------------------------
// global vars  (not thread-safe)
//
// These functions set global variables, which are then used during
// construction of requests.  Thus, they are not thread-safe.  For
// backward-compatibility, we're leaving the original interface in place.
// This will continue to be non-thread-safe.
// ---------------------------------------------------------------------------

void aws_reuse_connections(int b);
void aws_reset_connection();

void aws_set_id       ( char* const str );
void aws_set_key      ( char* const str );
void aws_set_keyid    ( char* const str );
int  aws_read_config  ( char* const ID );
void aws_set_debug    ( int d);
void aws_set_rrs      ( int r);

void s3_set_bucket    ( const char* const str );
void s3_set_host      ( const char* const str );
void s3_set_proxy     ( const char* const str );
void s3_set_mime      ( const char* const str );
void s3_set_acl       ( const char* const str );
void s3_set_byte_range( size_t offset, size_t length );
void s3_set_content_length( curl_off_t length ); // not working?

void s3_enable_EMC_extensions     ( int value );
void s3_enable_Scality_extensions ( int value );

void s3_https		                ( int value );
void s3_https_insecure		       ( int value );

 // not actually AWS or S3
void s3_chunked_transfer_encoding ( int value );
void s3_sproxyd                   ( int value );
void s3_http_digest               ( int value );



// ---------------------------------------------------------------------------
// global vars  (thread-safe)
//
// We're introducing thread-safe-variant configuration-functions.  These
// end in "_r" (like the POSIX rentrant variants, e.g. strtok_r()), and
// take an AWSContext argument.  This context can be installed into an
// IOBuf, to make thread-safe calls.
// ---------------------------------------------------------------------------

typedef struct {
   size_t offset;
   size_t length;
} ByteRange;

typedef struct AWSContext {
   CURL* ch;
   int   reuse_connections;
   int   reset_connection;
   int   inside;

   // int debug,                   /// <TBD: flag to control debugging options
   int   useRrs;                /// <Use reduced redundancy storage

   char* ID;                    /// <Current ID
   char* awsKeyID;              /// <AWS Key ID
   char* awsKey;                /// <AWS Key Material

   /// \todo Use SQSHost in SQS functions
   char* SQSHost;               /// <AWS SQS host
   char* S3Host;                /// <AWS S3 host
   char* S3Proxy;
   char* Bucket;
   char* MimeType;
   char* AccessControl;

   ByteRange  byte_range;        /// <reset automatically after next GET
   curl_off_t content_length;

   int  flags;
} AWSContext;


AWSContext* aws_context_new();
AWSContext* aws_context_clone(); // copy the default context
void        aws_context_reset(); // restore default settings

AWSContext* aws_context_clone_r(AWSContext* ctx);
void        aws_context_reset_r(AWSContext* ctx);
void        aws_context_free_r (AWSContext* ctx);

// void aws_reset_context();       // reset dflt ctx used by non-thread-safe fns
// void aws_reset_context_r( AWSContext* ctx );

void aws_reuse_connections_r( int b, AWSContext* ctx );
void aws_reset_connection_r ( AWSContext* ctx );

void aws_set_id_r       ( char* const str, AWSContext* ctx );
void aws_set_key_r      ( char* const str, AWSContext* ctx );
void aws_set_keyid_r    ( char* const str, AWSContext* ctx );
int  aws_read_config_r  ( char* const ID,  AWSContext* ctx );
void aws_set_debug_r    ( int d, AWSContext* ctx );
void aws_set_rrs_r      ( int r, AWSContext* ctx );

void s3_set_bucket_r    ( const char* const str, AWSContext* ctx );
void s3_set_host_r      ( const char* const str, AWSContext* ctx );
void s3_set_proxy_r     ( const char* const str, AWSContext* ctx );
void s3_set_mime_r      ( const char* const str, AWSContext* ctx );
void s3_set_acl_r       ( const char* const str, AWSContext* ctx );
void s3_set_byte_range_r( size_t offset, size_t length, AWSContext* ctx );
void s3_set_content_length_r( curl_off_t length, AWSContext* ctx ); // not working?

void s3_enable_EMC_extensions_r     ( int value, AWSContext* ctx );
void s3_enable_Scality_extensions_r ( int value, AWSContext* ctx );

void s3_https_r		               ( int value, AWSContext* ctx );
void s3_https_insecure_r		      ( int value, AWSContext* ctx );

 // not actually AWS or S3
void s3_chunked_transfer_encoding_r ( int value, AWSContext* ctx );
void s3_sproxyd_r                   ( int value, AWSContext* ctx );
void s3_http_digest_r               ( int value, AWSContext* ctx );




// ---------------------------------------------------------------------------
// IOBuf
// ---------------------------------------------------------------------------

// raw buffer gotten from server, e.g. via callback from libcurl internals. 
typedef struct _IOBufNode
{
   char*               buf;         // will be free'ed, unless is_static is non-zero
   size_t              len;         // actual size of <buf>
   size_t              write_count; // amount of <buf> that has been written.
   unsigned char       is_static;   // should we free <buf>, when freeing IOBufNode?

   struct _IOBufNode*  next;

} IOBufNode;

// linked-list of key/value pairs holding custom user meta-data
typedef struct _MetaNode {
   char*    key;
   char*    value;
   struct   _MetaNode* next;
} MetaNode;



// signatures for standard libcurl headerfunc / readfunc / writefunc
// are all defined in curl.h
#if 1
typedef size_t (*HeaderFnPtr)(void* ptr, size_t size, size_t nitems, void* stream);
typedef size_t (*ReadFnPtr)  (void* ptr, size_t size, size_t nmemb,  void* stream);
typedef size_t (*WriteFnPtr) (void* ptr, size_t size, size_t nmemb,  void* stream);
#elif 0
typedef size_t (*HeaderFnPtr)(char* ptr, size_t size, size_t nitems, void* stream);
typedef curl_read_callback   ReadFnPtr;
typedef curl_write_callback  WriteFnPtr;
#else
typedef size_t (*HeaderFnPtr)(char* ptr, size_t size, size_t nitems, void* stream);
#define ReadFnPtr   curl_read_callback
#define WriteFnPtr  curl_write_callback
#endif

/// IOBuf structure
/// Any changes here require co-maintenance of aws_iobuf_reset()
///
/// NOTE: Things marked "[*]" are not cleared by aws_iobuf_reset()
///
typedef struct IOBuf 
{
   IOBufNode*  first;
   IOBufNode*  last;
   HeaderFnPtr header_fn;     // [*] libcurl parsing the response header

   IOBufNode*  reading;
   char*       read_pos;
   WriteFnPtr  write_fn;      // [*] libcurl adding data to the IOBuf (during GET)

   IOBufNode*  writing;
   ReadFnPtr   read_fn;       // [*] libcurl sending from the IOBuf (during PUT/POST)

   size_t      len;           // total storage in IOBufNode buffers (sum of len)
   size_t      write_count;   // total written data (sum of write_counts)
   size_t      avail;         // total unread-data avail for read (e.g aws_iobuf_get_raw())
   size_t      growth_size;   // [*] controls default growth, in aws_iobuf_append fns

   char*       lastMod;
   char*       eTag;
   size_t      contentLen;    // might be more than 2 GiB
   MetaNode*   meta;          // x-amz-meta-* headers (1 per IOBufNode.buf)

   int         code;
   char*       result;          // string for <code>, (e.g. 'Not Found')

   AWSContext* context;        // [*] optional, for thread-safety
   void*       user_data;      // [*] e.g. to pass extra info to a readfunc
} IOBuf;



IOBuf* aws_iobuf_new ();
void   aws_iobuf_reset     ( IOBuf* bf ); // leaves some fields untouched
void   aws_iobuf_reset_lite(IOBuf* bf);   // leaves even more fields untouched
void   aws_iobuf_reset_hard( IOBuf* bf ); // deep clean
void   aws_iobuf_free      ( IOBuf* bf );

void   aws_iobuf_growth_size (IOBuf* b, size_t size);

void   aws_iobuf_append        ( IOBuf* b, char* d, size_t len );
void   aws_iobuf_append_str    ( IOBuf* b, char* d );
void   aws_iobuf_append_static ( IOBuf* b, char* d, size_t len );
void   aws_iobuf_append_dynamic( IOBuf* b, char* d, size_t len );

void   aws_iobuf_extend_static ( IOBuf* b, char* d, size_t len );
void   aws_iobuf_extend_dynamic( IOBuf* b, char* d, size_t len );

void   aws_iobuf_realloc( IOBuf* b );

size_t aws_iobuf_getline( IOBuf* b, char* Line, size_t size );
size_t aws_iobuf_get_raw( IOBuf* b, char* Line, size_t size );
size_t aws_iobuf_get_meta(IOBuf* b, char* Line, size_t size, const char* key );

void   aws_iobuf_headerfunc(IOBuf* b, HeaderFnPtr header_fn);
void   aws_iobuf_readfunc  (IOBuf* b, ReadFnPtr   read_fn);
void   aws_iobuf_writefunc (IOBuf* b, WriteFnPtr  write_fn);
void   aws_iobuf_context   (IOBuf* b, AWSContext* ctx);

void   aws_iobuf_chunked_transfer_encoding(IOBuf* b, int enable);

void   aws_iobuf_set_metadata( IOBuf* b, MetaNode* list);

const char* aws_metadata_get  (const MetaNode** list, const char* key);
void        aws_metadata_set  (      MetaNode** list, const char* key, const char* value);
void        aws_metadata_reset(      MetaNode** list);


// your headerfunc / readfunc / writefunc might want to call the default version
extern size_t aws_headerfunc( void* ptr, size_t size, size_t nitems, void* stream);
extern size_t aws_readfunc  ( void* ptr, size_t size, size_t nmemb,  void* stream );
extern size_t aws_writefunc ( void* ptr, size_t size, size_t nmemb,  void* stream );


// ---------------------------------------------------------------------------
// GET / PUT requests  (etc)
// ---------------------------------------------------------------------------

CURLcode aws_init();                // call once
void     aws_cleanup();             // call once

CURLcode  s3_head   ( IOBuf* b, char* const obj_name );
CURLcode  s3_get    ( IOBuf* b, char* const obj_name );
CURLcode  s3_put    ( IOBuf* b, char* const obj_name );
CURLcode  s3_post   ( IOBuf* b, char* const obj_name );
CURLcode  s3_delete ( IOBuf* b, char* const obj_name );

CURLcode  s3_head2  ( IOBuf* b, char* const obj_name, char* const src_file, IOBuf* response );
CURLcode  s3_get2   ( IOBuf* b, char* const obj_name, char* const src_file, IOBuf* response );
CURLcode  s3_put2   ( IOBuf* b, char* const obj_name, char* const src_file, IOBuf* response );
CURLcode  s3_post2  ( IOBuf* b, char* const obj_name, char* const src_file, IOBuf* response );

CURLcode  emc_put_append ( IOBuf* b, char* const obj_name );
CURLcode  emc_put2_append( IOBuf* b, char* const obj_name, char* const src_file, IOBuf* response );

int    sqs_create_queue ( IOBuf *b, char * const name );
int    sqs_list_queues ( IOBuf *b, char * const prefix );
int    sqs_get_queueattributes ( IOBuf *b, char * url, int *TimeOut, int *nMesg );
int    sqs_set_queuevisibilitytimeout ( IOBuf *b, char * url, int sec );
int    sqs_get_message ( IOBuf * b, char * const url, char * id  );
int    sqs_send_message ( IOBuf *b, char * const url, char * const msg );
int    sqs_delete_message ( IOBuf* bf, char * const url, char * receipt );





#  ifdef __cplusplus
}
#  endif

#endif
