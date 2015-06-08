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

/*!
  \mainpage

  This is a small library that provides Amazon Web Services binding
  for C programs.  
  
  The AWS4C leverages CURL and OPENSSL libraries for HTTP transfer and 
  cryptographic functions.

  The \ref todo list is here.

  The \ref bug list is here.

*/

/// \todo Include regression testing
/// \todo Run thing through valgrind

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

#include "aws4c.h"


/*!
  \defgroup internal Internal Functions
  \{
*/

static int    debug = 0;   /// <flag to control debugging options
static int    useRrs = 0;  /// <Use reduced redundancy storage

static char * ID       = NULL;  /// <Current ID
static char * awsKeyID = NULL;  /// <AWS Key ID
static char * awsKey   = NULL;  /// <AWS Key Material
static char * S3Host   = "s3.amazonaws.com";     /// <AWS S3 host
/// \todo Use SQSHost in SQS functions
static char * SQSHost  = "queue.amazonaws.com";  /// <AWS SQS host
static char * S3Proxy  = NULL;
static char * Bucket   = NULL;
static char * MimeType = NULL;
static char * AccessControl = NULL;

// S3Host and S3Proxy are initially static constants, but will be replaced
// with dynamically-allocated strings.  This lets us know whether to free,
// or not.
typedef enum {
   S3HOST_STATIC  = 0x01,
   S3PROXY_STATIC = 0x02
} AWS4C_FLAGS;
static int flags = (S3HOST_STATIC | S3PROXY_STATIC);

typedef struct {
   size_t offset;
   size_t length;
} ByteRange;

ByteRange  byte_range = {0, 0}; /* resets after next GET */

static int emc_compatibility = 0; /// <support EMC extended functionality

// If you call aws_reuse_connections() with non-zero value, then we'll try
// to reuse connection, instead of calling curl_easy_init() /
// curl_easy_cleanup() in every function.  aws_reset_connection causes a
// one-time reset.  (This is useful to eliminate client-side caching which
// may affect measure read bandwidth.)
static int    reuse_connections = 0;
static int    reset_connection  = 0;
static int    inside = 0;
CURL*         ch = NULL;
static void   aws_curl_enter();
static void   aws_curl_exit();


static void   __debug ( char *fmt, ... ) ;
static char * __aws_get_iso_date ();
static char * __aws_get_httpdate ();
static FILE * __aws_getcfg ();

static CURLcode s3_do_get ( IOBuf* b, char* const signature, 
                            char* const date, char* const resource,
                            int head_p,
                            char* const dst_fname, IOBuf* repsonse );

static CURLcode s3_do_put_or_post( IOBuf* b, char* const signature, 
                                   char* const date, char* const resource,
                                   int post_p,
                                   char* const src_fname, IOBuf* response );

static CURLcode s3_do_delete ( IOBuf* b, char* const signature, 
                               char* const date, char* const resource);

static void   aws_iobuf_append_internal(IOBuf* B, char* d, size_t len,
                                        int needs_alloc, int is_static);

static void   aws_iobuf_extend_internal(IOBuf* B, char* d, size_t len,
                                        int is_static);

static char*  __aws_sign ( char* const str );

static void   __chomp ( char * str );


#ifdef ENABLE_UNBASE64
/// Decode base64 into binary
/// \param input base64 text
/// \param length length of the input text
/// \return decoded data in  newly allocated buffer
/// \internal
///
/// This function allocates a buffer of the same size as the input
/// buffer and then decodes the given base64 encoded text into 
/// binary.   The result is placed into the allocated buffer. It is
/// the caller's responsibility to free this buffer
static char *unbase64(unsigned char *input, int length)
{
  BIO *b64, *bmem;

  /// Allocate and zero the buffer
  char *buffer = (char *)malloc(length);
  memset(buffer, 0, length);

  /// Decode the input into the newly allocated buffer
  /// \todo Check for errors during decode
  b64 = BIO_new(BIO_f_base64());
  bmem = BIO_new_mem_buf(input, length);
  bmem = BIO_push(b64, bmem);

  BIO_read(bmem, buffer, length);

  BIO_free_all(bmem);

  /// Return the decoded text
  return buffer;
}
#endif /* ENABLE_UNBASE64 */

/// Encode a binary into base64 buffer
/// \param input binary data  text
/// \param length length of the input text
/// \internal
/// \return a newly allocated buffer with base64 encoded data 
static char *__b64_encode(const unsigned char *input, int length)
{
  BIO *bmem, *b64;
  BUF_MEM *bptr;

  b64 = BIO_new(BIO_f_base64());
  bmem = BIO_new(BIO_s_mem());
  b64 = BIO_push(b64, bmem);
  BIO_write(b64, input, length);
  if(BIO_flush(b64)) ; /* make gcc 4.1.2 happy */
  BIO_get_mem_ptr(b64, &bptr);

  char *buff = (char *)malloc(bptr->length);
  memcpy(buff, bptr->data, bptr->length-1);
  buff[bptr->length-1] = 0;

  BIO_free_all(b64);

  return buff;
}

/// Chomp (remove the trailing '\n' from the string).
/// If there's a '\r' in the next-to-last position, remove that.
///
/// \param str string
///
static void __chomp ( char  * str )
{
  if ( str[0] == 0 )
    return;

  int ln = strlen(str) -1;
  if ( str[ln] == '\n' ) {
    str[ln] = 0;
    if ( ln == 0 )
      return ;

    ln--;
    if ( str[ln] == '\r' )
      str[ln] = 0;
  }
}

/// Handles reception of the data
/// \param ptr pointer to the incoming data
/// \param size size of the data member
/// \param nmemb number of data memebers
/// \param stream pointer to I/O buffer
/// \return number of bytes processed
size_t aws_writefunc ( void * ptr, size_t size, size_t nmemb, void * stream )
{
  __debug ( "DATA RECV %d items of size %d ",  nmemb, size );
  aws_iobuf_append ( stream, ptr, nmemb*size );
  return nmemb * size;
}

/// Suppress outputs to stdout
static size_t aws_writedummyfunc ( void * ptr, size_t size, size_t nmemb, void * stream )
{
  __debug ( "writedummy -- stifling %d items of size %d ",  nmemb, size );
  return nmemb * size;
}

/// Handles sending of the data
/// \param ptr pointer to the incoming data
/// \param size size of the data member
/// \param nmemb number of data memebers
/// \param stream pointer to I/O buffer
/// \return number of bytes written
size_t aws_readfunc ( void * ptr, size_t size, size_t nmemb, void * stream )
{
  char * Ln = ptr;
  // int sz = aws_iobuf_getline ( stream, ptr, size*nmemb);
  size_t sz = aws_iobuf_get_raw ( stream, ptr, size*nmemb);
  __debug ( "Sent[%3lu] %s", sz, Ln );
  return sz;
}

/// Process incoming header
/// \param ptr pointer to the incoming data
/// \param size size of the data member
/// \param nmemb number of data memebers
/// \param stream pointer to I/O buffer
/// \return number of bytes processed
size_t aws_headerfunc ( void * ptr, size_t size, size_t nmemb, void * stream )
{
  // __debug("header -- reading %d * %d bytes from '%s'", nmemb, size, ptr);

  IOBuf * b = stream;

  if (!strncmp( ptr, "HTTP/1.1", 8 )) {
    if (b->result)
      free(b->result);          /* don't leak memory */
    b->result = strdup( ptr + 9 );
    __chomp(b->result);
    b->code   = atoi( ptr + 9 );
  }
  else if ( !strncmp( ptr, "ETag: ", 6 )) {
    if (b->eTag)
      free(b->eTag);            /* don't leak memory */
    b->eTag = strdup( ptr + 6 );
    __chomp(b->eTag);
  }
  else if ( !strncmp( ptr, "Last-Modified: ", 14 )) {
    if (b->lastMod)
      free(b->lastMod);         /* don't leak memory */
    b->lastMod = strdup( ptr + 15 );
    __chomp(b->lastMod);
  }
  else if ( !strncmp( ptr, "Content-Length: ", 15 )) {
     b->contentLen = strtoul( ptr + 16, NULL, 10 );
  }
  else if ( !strncmp( ptr, "x-amz-meta-", 11 )) {
     char* key     = ptr+11;
     char* key_end = strchr(key, ':');
     if (key_end) {
        *key_end = 0;
        char* value = key_end +2; /* skip ": " */
        __chomp(value);
        aws_metadata_set(&(b->meta), key, value);
     }
  }

  return nmemb * size;
}


/// Get Data for authentication of SQS request
/// \return date in ISO format
static char * __aws_get_iso_date ()
{
  static char dTa[256];
  time_t t = time(NULL);
  struct tm * gTime = gmtime ( & t );

  memset ( dTa, 0 , sizeof(dTa));
  strftime ( dTa, sizeof(dTa), "%FT%H:%M:%SZ", gTime );
  __debug ( "Request Time: %s", dTa );
  return dTa;
}

#ifdef ENABLE_DUMP
/// Dump current state
/// \internal
static void Dump ()
{
  printf ( "----------------------------------------\n");
  printf ( "ID     : %-40s \n", ID );
  printf ( "KeyID  : %-40s \n", awsKeyID );
  printf ( "Key    : %-40s \n", awsKey );
  printf ( "S3  Host   : %-40s \n", S3Host );
  printf ( "S3  Proxy  : %-40s \n", S3Proxy );
  printf ( "SQS Host   : %-40s \n", SQSHost );
  printf ( "Bucket : %-40s \n", Bucket );
  printf ( "----------------------------------------\n");
}
#endif /* ENABLE_DUMP */


/// Print debug output
/// \internal
/// \param fmt printf like formating string
static void __debug ( char *fmt, ... ) {
  /// If debug flag is not set we won't print anything
  if ( ! debug ) return ;
  /// Otherwise process the arguments and print the result
  va_list args;
  va_start( args, fmt );
  fprintf( stderr, "DBG: " );
  vfprintf( stderr, fmt, args );
  fprintf( stderr, "\n" );
  va_end( args );
}


/// Get Request Date
/// \internal
/// \return date in HTTP format
static char * __aws_get_httpdate ()
{
  static char dTa[256];
  time_t t = time(NULL);
  struct tm * gTime = gmtime ( & t );
  memset ( dTa, 0 , sizeof(dTa));
  strftime ( dTa, sizeof(dTa), "%a, %d %b %Y %H:%M:%S +0000", gTime );
  __debug ( "Request Time: %s", dTa );
  return dTa;
}

/// Internal function to get configuration file
static FILE * __aws_getcfg ()
{
  int rv;
  char ConfigFile[256];
  /// Compose FileName and check
  snprintf ( ConfigFile, sizeof(ConfigFile) -3, "%s/.awsAuth", getenv("HOME"));
  __debug ( "Config File %s", ConfigFile );

  struct stat sBuf;
  rv = stat ( ConfigFile, &sBuf );
  if ( rv == -1 ) return NULL;

  
  if ( sBuf.st_mode & 066   ||
       sBuf.st_uid != getuid () ) {
    fprintf ( stderr, "I refuse to read your credentials from %s as this "
              "file is readable by, writable by or owned by someone else."
              "Try chmod 600 %s", ConfigFile, ConfigFile );
    return NULL;
  }

  return fopen ( ConfigFile, "r" );
}


/// Get S3 Request signature
/// \internal
/// \param resource -- URI of the object
/// \param resSize --  size of the resource buffer
/// \param date -- HTTP date
/// \param method -- HTTP method
/// \param bucket -- bucket 
/// \param file --  file
/// \return fills up resource and date parameters, also 
///         returns request signature to be used with Authorization header
static char * GetStringToSign ( char *       resource,
                                int          resSize, 
                                char **      date,
                                char * const method,
                                MetaNode*    metadata,
                                char * const bucket,
                                char * const file )
{
  char  reqToSign[2048];
  char  acl[32];
  char  rrs[64];

  const size_t MAX_META = 2048;
  char         meta[MAX_META];
  MetaNode*    pair;
  

  /// \todo Change the way RRS is handled.  Need to pass it in
  
  * date = __aws_get_httpdate();

  memset ( resource, 0, resSize);
  if ( bucket )
    snprintf ( resource, resSize,"%s/%s", bucket, file );
  else
    snprintf ( resource, resSize,"%s", file );

  if (AccessControl)
    snprintf( acl, sizeof(acl), "x-amz-acl:%s\n", AccessControl);
  else
    acl[0] = 0;

  // print meta-data into meta[], until we run out of room
  size_t offset=0;
  size_t remain=MAX_META -1;    /* assure there's room for final NULL */
  for (pair=metadata; pair; pair=pair->next) {
     size_t expect = strlen(pair->key) + strlen(pair->value) + 13;
     if (expect > remain)
        break;                  /* don't print partial key/value pairs */
     int count = snprintf( &meta[offset], remain, "x-amz-meta-%s:%s\n",
                           pair->key, pair->value);
     if (count != expect) {
         fprintf(stderr, "Error computing meta-data size: expect=%ld actual=%d\n",
                 expect, count);
         exit(1);
     }
     remain -= count;
     offset += count;
  }
  meta[offset] = 0;
  

  if (useRrs)
    strncpy( rrs, "x-amz-storage-class:REDUCED_REDUNDANCY\n", sizeof(rrs));  
  else
    rrs[0] = 0;


  snprintf ( reqToSign, sizeof(reqToSign),"%s\n\n%s\n%s\n%s%s%s/%s",
             method,
             MimeType ? MimeType : "",
             *date,
             acl,
             meta,
             rrs,
             resource );

  // EU: If bucket is in virtual host name, remove bucket from path
  if (bucket && strncmp(S3Host, bucket, strlen(bucket)) == 0)
    snprintf ( resource, resSize,"%s", file );

  return __aws_sign(reqToSign);
}

static void __aws_urlencode ( char * src, char * dest, int nDest )
{
  int i;
  int n;
  memset ( dest, 0, nDest );
  __debug ( "Encoding: %s", src );
  const char * badChrs = " \n$&+,/:;=?@";
  const char * hexDigit = "0123456789ABCDEF";

  n = 0;
  for ( i = 0 ; src[i] ; i ++ ) {
    if ( n + 5 > nDest ) {
      puts ( "URLEncode:: Dest buffer to small.. can't continue \n" );
      exit(0);
    }
    if ( strchr ( badChrs, src[i] )) {
      unsigned char c = src[i];
      dest[n++] = '%'; 
      dest[n++] = hexDigit [(c >> 4 ) & 0xF ];
      dest[n++] = hexDigit [c & 0xF ];
    }
    else dest[n++] = src[i];
  }
  __debug ( "Encoded To: %s", dest );
}


// This affects whether AWS_CURL_ENTER() / AWS_CURL_EXIT() will use
// curl_easy_reset() or will entirely close the curl connection.
void aws_reuse_connections(int val) {
   reuse_connections = val;
}

// This causes the next curl-request to reset the connection.  It causes
// the connection to be reset only once.  This is useful, for example, to
// drop client-side caches after writing, which may artificially enhance
// read-bandwidth.  If reuse_connections is non-zero, connections will
// still be reused, after the one-time reset.
void aws_reset_connection() {
   if (inside)
      curl_easy_setopt(ch, CURLOPT_FORBID_REUSE, 1); /* current request */
   else
      reset_connection = 1;     /* next request */
}

// See also: curl_reuse_connections()
#define     AWS_CURL_ENTER()                    \
   aws_curl_enter(__FILE__, __LINE__)

// Don't call this directly.  Use AWS_CURL_ENTER().
static void aws_curl_enter(const char* fname, int line) {
   __debug("aws_curl_enter: '%s', line %d\n", fname, line);

   if (! ch) {
      ch = curl_easy_init();
      if (! ch) {
         fprintf(stderr, "curl_easy_init failed in '%s' at line %d\n",
                 fname, line);
         exit(1);
      }
   }
   else if (reuse_connections) {
      curl_easy_reset(ch); // restore all options to their default state
      if (reset_connection)
         curl_easy_setopt(ch, CURLOPT_FRESH_CONNECT, 1);
   }
   else {

      // Either: (1) reuse_connections was true when aws_curl_exit() was
      // called, then was set to false, or (2) reuse_connections was false
      // and an aws4c library-function failed to call aws_curl_exit() after
      // calling aws_curl_enter().  Anyhow, it's false now, and we have an
      // old handle.
      curl_easy_cleanup(ch);
      ch = NULL;                  /* this is safe, after curl_easy_cleanup() */

      aws_curl_enter(fname, line);
   }

   inside = 1;
}


// See also: curl_reuse_connections()
#define     AWS_CURL_EXIT()                     \
   aws_curl_exit(__FILE__, __LINE__)

// Don't call this directly.  Use AWS_CURL_EXIT().
static void aws_curl_exit(const char* fname, int line) {
   __debug("aws_curl_exit: '%s', line %d\n", fname, line);

   assert(ch);

   reset_connection = 0;        /* aws_curl_exit() implies reset was performed? */
   inside = 0;
}


static int SQSRequest ( IOBuf *b, char * verb, char * const url )
{
   AWS_CURL_ENTER();
  struct curl_slist *slist=NULL;

  curl_easy_setopt ( ch, CURLOPT_URL, url );
  curl_easy_setopt ( ch, CURLOPT_VERBOSE, debug );
  curl_easy_setopt ( ch, CURLOPT_INFILESIZE, b->len );
  curl_easy_setopt ( ch, CURLOPT_POST, 1 );
  curl_easy_setopt ( ch, CURLOPT_POSTFIELDSIZE , 0 );

  curl_easy_setopt ( ch, CURLOPT_HEADERDATA, b );
  curl_easy_setopt ( ch, CURLOPT_HEADERFUNCTION, (b->header_fn
                                                  ? b->header_fn
                                                  : aws_headerfunc) );
  curl_easy_setopt ( ch, CURLOPT_WRITEDATA, b );
  curl_easy_setopt ( ch, CURLOPT_WRITEFUNCTION, (b->write_fn
                                                 ? b->write_fn
                                                 : aws_writefunc) );
  curl_easy_setopt ( ch, CURLOPT_READDATA, b );
  curl_easy_setopt ( ch, CURLOPT_READFUNCTION, (b->read_fn
                                                ? b->read_fn
                                                : aws_readfunc) );

  if (S3Proxy)
     curl_easy_setopt ( ch, CURLOPT_PROXY, S3Proxy);

  int  sc  = curl_easy_perform(ch);
  /** \todo check the return code  */
  __debug ( "Return Code: %d ", sc );
  
  curl_slist_free_all(slist);

  // NOTE: There was no curl_easy_cleanup() here.  Was that a bug, or was
  //       that deliberate?  If it was a bug, then uncomment the
  //       "AWS_CURL_EXIT()", here, and delete this comment.
  //
  //  AWS_CURL_EXIT();

  return sc;
}

static char * SQSSign ( char * str )
{
  char RealSign[1024];
  char * signature = __aws_sign(str);

  __aws_urlencode ( signature, RealSign, sizeof(RealSign));
    
  free ( signature );
  return strdup(RealSign);
}



/*!
  \}
*/


/*!
  \defgroup conf Configuration Functions
  \{
*/

/// Initialize  the library.
///
/// NOTE: From the manpage:
///
///    This function is not thread safe. You must not call it when any
///    other thread in the program (i.e. a thread sharing the same memory)
///    is running. This doesn't just mean no other thread that is using
///    libcurl. Because curl_global_init() calls functions of other
///    libraries that are similarly thread unsafe, it could conflict with
///    any other thread that uses these other libraries.
///
CURLcode aws_init () {
   return curl_global_init(CURL_GLOBAL_ALL);
}
void     aws_cleanup () {
   curl_global_cleanup();
}


/// Set debuging output
/// \param d  when non-zero causes debugging output to be printed
void aws_set_debug (int d) {
  debug = d;
}

/// \brief Set AWS account ID to be read from .awsAuth file
/// \param id new account ID
void aws_set_id ( char * const id )     
{ ID = ((id == NULL) ? getenv("USER") : strdup(id)); }

/// Set AWS account access key
/// \param key new AWS authentication key
void aws_set_key ( char * const key )   
{ awsKey = ((key == NULL) ? NULL : strdup(key)); }

/// Set AWS account access key ID
/// \param keyid new AWS key ID
void aws_set_keyid ( char * const keyid ) 
{ awsKeyID = keyid == NULL ? NULL :  strdup(keyid);}

/// Set reduced redundancy storage
/// \param r  when non-zero causes puts to use RRS
void aws_set_rrs (int r) 
{ useRrs = r; }




/// Read AWS authentication records
/// \param id  user ID
int aws_read_config ( char * const id )
{
  aws_set_id ( id );
  aws_set_keyid ( NULL );
  aws_set_key   ( NULL   );

  /// Open File
  /// Make sure that file permissions are set right
  __debug ( "Reading Config File ID[%s]", ID );
  FILE * f = __aws_getcfg();
  if ( f == NULL ) { perror ("Error opening config file"); exit(1); }
  

  /// Read Lines
  char line[1024];
  int  ln = 0;
  while ( !feof(f)) {
    ln++;
    memset (line,0,sizeof(line));
    fgets ( line, sizeof(line), f );

    /// Skip Comments
    if ( line[0] == '#' ) continue;
    if ( line[0] == 0 ) continue;

    __chomp ( line );
      

    /// Split the line on ':'
    char * keyID = strchr(line,':');
    if ( keyID == NULL ) {
      printf ( "Syntax error in credentials file line %d, no keyid\n", ln );
      exit(1);
    }
    *keyID = 0; keyID ++;

    char * key = strchr(keyID,':');
    if ( key == NULL ) {
      printf ( "Syntax error in credentials file line %d, no key\n", ln );
      exit(1);
    }
    *key = 0; key ++;
      
      
    /// If the line is correct Set the IDs
    if ( !strcmp(line,id)) {
      aws_set_keyid ( keyID );
      aws_set_key   ( key   );
      break;
    }
  }
  /// Return error if not found
  if ( awsKeyID == NULL ) {
     __debug("Didn't find user %s in config-file\n", id);
     return -1;
  }

  return 0;
}

/*!
  \}
*/




/*!
  \defgroup s3 S3 Interface Functions
  \{
*/


/// Set S3 host
void s3_set_host ( const char * const str ) {

   if (S3Host && str && !strcmp(str, S3Host))
      return;

   assert(!inside);
   if (ch) {
      curl_easy_cleanup(ch);
      ch = NULL;
   }
   if (S3Host && !(flags & S3HOST_STATIC))
      free(S3Host);
   S3Host = ((str == NULL) ? NULL :  strdup(str));
   flags &=  ~(S3HOST_STATIC);
}

/// Set S3 Proxy NOTE: libcurl allows separate specifications of proxy,
/// proxy_port, and proxy_type, but they aren't necesssary.  You can put
/// them all into the proxy-name string (e.g. "socks5://xx.xx.xx:port".
/// Therefore, we only provide a way to set the proxy.
///
/// Set to NULL (the default), to stop using a proxy.
void s3_set_proxy ( const char * const str ) {

   if (S3Proxy && str && !strcmp(str, S3Proxy))
      return;

   assert(!inside);
   if (ch) {
      curl_easy_cleanup(ch);
      ch = NULL;
   }
   if (S3Proxy && !(flags & S3PROXY_STATIC))
      free(S3Proxy);
   S3Proxy = ((str == NULL) ? NULL :  strdup(str));
   flags &=  ~(S3PROXY_STATIC);
}

/// Select current S3 bucket
/// \param str bucket ID
void s3_set_bucket ( const char * const str ) 
{ Bucket = ((str) ? strdup(str) : NULL); }

/// Set S3 MimeType
void s3_set_mime ( const char * const str )
{ MimeType = ((str) ? strdup(str) : NULL); }

/// Set byte-range.  NOTE: resets after the next GET, PUT, or POST.
/// If emc-compatibility is enabled, you can use this to append to objects,
/// or to allow parallel updates.
void s3_set_byte_range ( size_t offset, size_t length ) {
   byte_range.offset = offset;
   byte_range.length = length;
}

/// Set S3 AccessControl
void s3_set_acl ( const char * const str )
{ AccessControl = ((str) ? strdup(str) : NULL); }


/// EMC supports some extended functionality, such as using byte-ranges
/// during PUT / POST, in order to write multi-parts, or to append to an
/// object.  These operations are not allowed in "pure" S3.  You must
/// enable EMC-compatibility in order to allow the operations.
void s3_enable_EMC_extensions ( int value )
{ emc_compatibility = value; }






/// Upload the contents of <b> into object <obj_name>, under currently-selected bucket
/// \param b          I/O buffer
/// \param obj_name   name of the target-object (not including the bucket).
CURLcode s3_put ( IOBuf* b, char * const obj_name )
{
  char * const method = "PUT";
  char  resource [1024];
  char * date = NULL;

  char * signature = GetStringToSign ( resource, sizeof(resource), 
                                       &date, method, b->meta, Bucket, obj_name ); 
  CURLcode sc = s3_do_put_or_post( b, signature, date, resource, 0, NULL, NULL ); 
  free ( signature );
  return sc;
}
/// Like S3_put(), but with a little more control.
/// \param file       local file to be uploaded. (If NULL, then we assume data is in <b>.)
/// \param response   if non-null, gets the XML response from the server.
CURLcode s3_put2 ( IOBuf* b, char * const obj_name, char* const src_file, IOBuf* response )
{
  char * const method = "PUT";
  char  resource [1024];
  char * date = NULL;

  char * signature = GetStringToSign ( resource, sizeof(resource), 
                                       &date, method, b->meta, Bucket, obj_name ); 
  CURLcode sc = s3_do_put_or_post( b, signature, date, resource, 0, src_file, response ); 
  free ( signature );
  return sc;
}

/// append to an object.  (Requires EMC-extensions to S3.)
/// s3_do_put_or_post() will complain, if you don't have emc_compatibility enabled.
CURLcode emc_put_append( IOBuf* b, char * const obj_name ) {
   s3_set_byte_range(-1,-1);
   return s3_put(b, obj_name);
}
CURLcode emc_put2_append ( IOBuf* b, char * const obj_name, char* const src_file, IOBuf* response ) {
   s3_set_byte_range(-1,-1);
   return s3_put2(b, obj_name, src_file, response);
}


/// like s3_put(), but with POST
CURLcode s3_post ( IOBuf* b, char* const obj_name )
{
  char * const method = "POST";
  char  resource [1024];
  char * date = NULL;

  char * signature = GetStringToSign ( resource, sizeof(resource), 
                                       &date, method, b->meta, Bucket, obj_name ); 
  CURLcode sc = s3_do_put_or_post( b, signature, date, resource, 1, NULL, NULL ); 
  free ( signature );
  return sc;
}
/// like s3_put2()(), but with POST
CURLcode s3_post2 ( IOBuf* b, char* const obj_name, char* const src_file, IOBuf* response )
{
  char * const method = "POST";
  char  resource [1024];
  char * date = NULL;

  char * signature = GetStringToSign ( resource, sizeof(resource), 
                                       &date, method, b->meta, Bucket, obj_name ); 
  CURLcode sc = s3_do_put_or_post( b, signature, date, resource, 1, src_file, response ); 
  free ( signature );
  return sc;
}


/// Download the file from the current bucket
/// \param b I/O buffer
/// \param file filename 
CURLcode s3_get ( IOBuf * b, char * const obj_name )
{
  char * const method = "GET";
  char  resource [1024];
  char * date = NULL;

  char * signature = GetStringToSign ( resource, sizeof(resource), 
                                       &date, method, b->meta, Bucket, obj_name ); 
  CURLcode sc = s3_do_get( b, signature, date, resource, 0, NULL, NULL ); 
  free ( signature );
  return sc;
}
CURLcode s3_get2 ( IOBuf * b, char * const obj_name, char* const src_file, IOBuf* response )
{
  char * const method = "GET";
  char  resource [1024];
  char * date = NULL;

  char * signature = GetStringToSign ( resource, sizeof(resource), 
                                       &date, method, b->meta, Bucket, obj_name ); 
  CURLcode sc = s3_do_get( b, signature, date, resource, 0, src_file, response ); 
  free ( signature );
  return sc;
}


/// send a HEAD request (i.e. get obj metadata, without contents)
CURLcode s3_head ( IOBuf * b, char * const obj_name )
{
  char * const method = "HEAD";
  char  resource [1024];
  char * date = NULL;

  char * signature = GetStringToSign ( resource, sizeof(resource), 
                                       &date, method, b->meta, Bucket, obj_name ); 
  CURLcode sc = s3_do_get( b, signature, date, resource, 1, NULL, NULL ); 
  free ( signature );
  return sc;
}
CURLcode s3_head2 ( IOBuf * b, char * const obj_name, char* const fname, IOBuf* response )
{
  char * const method = "HEAD";
  char  resource [1024];
  char * date = NULL;

  char * signature = GetStringToSign ( resource, sizeof(resource), 
                                       &date, method, b->meta, Bucket, obj_name ); 
  CURLcode sc = s3_do_get( b, signature, date, resource, 1, fname, response ); 
  free ( signature );
  return sc;
}



/// Delete the file from the currently selected bucket
/// \param file filename
CURLcode s3_delete ( IOBuf * b, char * const obj_name )
{
  char * const method = "DELETE";
  
  char  resource [1024];
  char * date = NULL;
  char * signature = GetStringToSign ( resource, sizeof(resource), 
                                       &date, method, b->meta, Bucket, obj_name ); 
  CURLcode sc = s3_do_delete( b, signature, date, resource ); 
  free ( signature );

  return sc;
}



/// If <post_p> is non-zero, then do a POST.  Otherwise, do a PUT.
///
/// If <src_fname> is provided, it is assumed to be a file for uploading.
/// Otherwise, we assume the data is contained in <read_b>.
///
/// NOTE: In the case of a post with post-fields, just add them to your
///       obj_name, in the call to s3_post(), like this:
///
///           s3_post(bf, "obj_name?uploads");
///
/// NOTE: Not sure about old PUT behavior, but, for POST, we need to receive
///       XML from the server.  Therefore we need writefunc to write into
///       an IOBuf.  We could reuse the one that is used by the readfunc
///       (because the writefunc should only operate after the readfunc is
///       complete, right?), but this seems risky and confusing.  Instead,
///       we support a separate IOBuf, to receive server-response output
///       from the writefunc.
///
// NOTE: We now allow "chunked transfer-encoding" with streaming writes.
///      You would set up the IOBuf with a negative write-count, and
///      install a custom readfunc().  See example in test_aws4c.c (case
///      12)

static CURLcode
s3_do_put_or_post ( IOBuf *read_b, char * const signature, 
                    char * const date, char * const resource,
                    int post_p,
                    char* src_fname, IOBuf* write_b )
{
  char  Buf[1024];
  FILE* upload_fp = NULL;

  AWS_CURL_ENTER();

  int chunked = (read_b->flags & IOBF_CTE);

  // accumulate all custom headers into <slist>
  struct curl_slist *slist=NULL;
  if (MimeType) {
    snprintf ( Buf, sizeof(Buf), "Content-Type: %s", MimeType );
    slist = curl_slist_append(slist, Buf );
  }
  if (AccessControl) {
    snprintf ( Buf, sizeof(Buf), "x-amz-acl: %s", AccessControl );
    slist = curl_slist_append(slist, Buf );
  }
  if (useRrs) {
    strncpy ( Buf, "x-amz-storage-class: REDUCED_REDUNDANCY", sizeof(Buf) );
    slist = curl_slist_append(slist, Buf );
  }
  if (byte_range.length) {
     if (! emc_compatibility) {
        fprintf(stderr, "ERROR: PUT/POST with 'byte-range' "
                "not supported without EMC-extensions\n");
        exit(1);
     }
     else if (byte_range.offset == (size_t)-1)
        snprintf ( Buf, sizeof(Buf), "Range: bytes=-1-"); /* "append" */
     else {
        snprintf ( Buf, sizeof(Buf), "Range: bytes=%ld-%ld",
                   byte_range.offset,
                   byte_range.offset + byte_range.length -1);
     }
     slist = curl_slist_append(slist, Buf);
     memset(&byte_range, 0, sizeof(ByteRange));       /* reset after each use */
  }

  snprintf ( Buf, sizeof(Buf), "Date: %s", date );
  slist = curl_slist_append(slist, Buf );

  snprintf ( Buf, sizeof(Buf), "Authorization: AWS %s:%s", awsKeyID, signature );
  slist = curl_slist_append(slist, Buf );

  // install user meta-data
  MetaNode* meta;
  for (meta=read_b->meta; meta; meta=meta->next) {
     snprintf ( Buf, sizeof(Buf), "x-amz-meta-%s: %s", meta->key, meta->value );
     slist = curl_slist_append(slist, Buf );
  }

  // sounds like CTE is supposed to be the default, if length is not
  // provided, but we might as well be explicit.
  if (chunked)
     slist = curl_slist_append(slist, "Transfer-Encoding: chunked");

  snprintf ( Buf, sizeof(Buf), "http://%s/%s", S3Host , resource );

  curl_easy_setopt ( ch, CURLOPT_HTTPHEADER, slist);
  curl_easy_setopt ( ch, CURLOPT_URL, Buf );
  curl_easy_setopt ( ch, CURLOPT_VERBOSE, debug );
  curl_easy_setopt ( ch, CURLOPT_UPLOAD, 1 );
  curl_easy_setopt ( ch, CURLOPT_FOLLOWLOCATION, 1 );

  curl_easy_setopt ( ch, CURLOPT_HEADERDATA, read_b ); /* keeping original code */
  curl_easy_setopt ( ch, CURLOPT_HEADERFUNCTION, ( read_b->header_fn
                                                   ? *(read_b->header_fn)
                                                   : aws_headerfunc) );

  if (S3Proxy)
     curl_easy_setopt ( ch, CURLOPT_PROXY, S3Proxy);

  // If caller provided an IOBuf to recv the response from the server,
  // we'll arrange the writefunc to fill it.  Otherwise, if debugging is
  // disabled, writedummyfunc will suppress the output.  Otherwise, if
  // debugging is on, the response will go to stdout.
  if (write_b) {
    curl_easy_setopt ( ch, CURLOPT_WRITEDATA, write_b );
    curl_easy_setopt ( ch, CURLOPT_WRITEFUNCTION, ( write_b->write_fn
                                                    ? *(write_b->write_fn)
                                                    : aws_writefunc) );
  }
  else if (!debug)
    curl_easy_setopt ( ch, CURLOPT_WRITEFUNCTION, aws_writedummyfunc );


  // maybe upload a file, instead of appending raw-data into the request via read_b
  if (src_fname) {
     struct stat stat_info;

     stat(src_fname, &stat_info);
     upload_fp = fopen(src_fname, "r");
     if (! upload_fp) {
        fprintf(stderr, "s3_do_put_or_post, couldn't open '%s' for reading: %s\n",
                src_fname, strerror(errno));
        exit(1);
     }

     curl_easy_setopt ( ch, CURLOPT_READDATA, upload_fp );
     curl_easy_setopt ( ch, CURLOPT_READFUNCTION, (read_b->read_fn
                                                   ? *(read_b->read_fn)
                                                   : NULL) );
     curl_easy_setopt ( ch, CURLOPT_INFILESIZE_LARGE, (curl_off_t)stat_info.st_size);
  }
  else {
     curl_easy_setopt ( ch, CURLOPT_READDATA, read_b );
     curl_easy_setopt ( ch, CURLOPT_READFUNCTION, (read_b->read_fn
                                                   ? *(read_b->read_fn)
                                                   : aws_readfunc) );

     if (! chunked)
        curl_easy_setopt ( ch, CURLOPT_INFILESIZE, read_b->write_count );
  }


  // if <post_p> is non-zero, use POST, instead of PUT
  if (post_p) {
    //    curl_easy_setopt ( ch, CURLOPT_POST, 1);    /* doesn't cause a POST? */
    //    curl_easy_setopt ( ch, CURLOPT_POSTFIELDS, "uploads" ); /* DEBUGGING */
    curl_easy_setopt(ch, CURLOPT_CUSTOMREQUEST, "POST");
  }
  else
    curl_easy_setopt(ch, CURLOPT_PUT, 1);


  CURLcode sc  = curl_easy_perform(ch);
  __debug ( "Return Code: %d ", sc );
  
  curl_slist_free_all(slist);
  if (upload_fp)
     fclose(upload_fp);

  AWS_CURL_EXIT();
  return sc;
}



static CURLcode
s3_do_get ( IOBuf* b, char* const signature, 
            char* const date, char* const resource,
            int head_p,
            char* const dst_fname, IOBuf* response )
{
  char  Buf[1024];
  FILE* download_fp = NULL;

  AWS_CURL_ENTER();
  struct curl_slist *slist=NULL;

  slist = curl_slist_append(slist, "If-Modified-Since: Tue, 26 May 2009 18:58:55 GMT" );
  slist = curl_slist_append(slist, "ETag: \"6ea58533db38eca2c2cc204b7550aab6\"");

  if (byte_range.length) {
     snprintf ( Buf, sizeof(Buf), "Range: bytes=%ld-%ld",
                byte_range.offset,
                byte_range.offset + byte_range.length -1);
     slist = curl_slist_append(slist, Buf);
     memset(&byte_range, 0, sizeof(ByteRange));       /* reset after each use */
  }

  snprintf ( Buf, sizeof(Buf), "Date: %s", date );
  slist = curl_slist_append(slist, Buf );

  snprintf ( Buf, sizeof(Buf), "Authorization: AWS %s:%s", awsKeyID, signature );
  slist = curl_slist_append(slist, Buf );

  snprintf ( Buf, sizeof(Buf), "http://%s/%s", S3Host, resource );

  curl_easy_setopt ( ch, CURLOPT_HTTPHEADER, slist);
  curl_easy_setopt ( ch, CURLOPT_URL, Buf );
  curl_easy_setopt ( ch, CURLOPT_VERBOSE, debug );

  curl_easy_setopt ( ch, CURLOPT_HEADERDATA, b );
  curl_easy_setopt ( ch, CURLOPT_HEADERFUNCTION, (b->header_fn ? b->header_fn : aws_headerfunc) );

  if (S3Proxy)
     curl_easy_setopt ( ch, CURLOPT_PROXY, S3Proxy);

  // maybe turn this into a HEAD request
  curl_easy_setopt ( ch, CURLOPT_NOBODY, head_p );
  curl_easy_setopt ( ch, CURLOPT_HEADER, head_p );

  // maybe download to a file, instead of appending raw-data to <b>
  if (dst_fname) {
    download_fp = fopen(dst_fname, "w");
    if (! download_fp) {
       fprintf(stderr, "s3_do_get, couldn't open '%s' for writing: %s\n",
               dst_fname, strerror(errno));
      exit(1);
    }

    curl_easy_setopt ( ch, CURLOPT_WRITEDATA,     download_fp );
    curl_easy_setopt ( ch, CURLOPT_WRITEFUNCTION, (b->write_fn ? b->write_fn : NULL) );
  }
  else {
     curl_easy_setopt ( ch, CURLOPT_WRITEDATA,     b );
     curl_easy_setopt ( ch, CURLOPT_WRITEFUNCTION, (b->write_fn ? b->write_fn : aws_writefunc) );
  }


  CURLcode sc  = curl_easy_perform(ch);
  /** \todo check the return code  */
  __debug ( "Return Code: %d ", sc );
  
  curl_slist_free_all(slist);
  if (download_fp)
     fclose(download_fp);

  AWS_CURL_EXIT();
  return sc;
}


static CURLcode
s3_do_delete ( IOBuf *b, char * const signature, 
               char * const date, char * const resource )
{
  char Buf[1024];

  AWS_CURL_ENTER();
  struct curl_slist *slist=NULL;


  snprintf ( Buf, sizeof(Buf), "Date: %s", date );
  slist = curl_slist_append(slist, Buf );
  snprintf ( Buf, sizeof(Buf), "Authorization: AWS %s:%s", awsKeyID, signature );
  slist = curl_slist_append(slist, Buf );

  snprintf ( Buf, sizeof(Buf), "http://%s/%s", S3Host, resource );

  curl_easy_setopt ( ch, CURLOPT_CUSTOMREQUEST, "DELETE");
  curl_easy_setopt ( ch, CURLOPT_HTTPHEADER, slist);
  curl_easy_setopt ( ch, CURLOPT_URL, Buf );
  curl_easy_setopt ( ch, CURLOPT_VERBOSE, debug );

  curl_easy_setopt ( ch, CURLOPT_HEADERDATA, b );
  curl_easy_setopt ( ch, CURLOPT_HEADERFUNCTION, (b->header_fn ? b->header_fn : aws_headerfunc) );

  if (S3Proxy)
     curl_easy_setopt ( ch, CURLOPT_PROXY, S3Proxy);

  CURLcode sc  = curl_easy_perform(ch);
  /** \todo check the return code  */
  __debug ( "Return Code: %d ", sc );

  
  curl_slist_free_all(slist);
  AWS_CURL_EXIT();

  return sc;
}




static char* __aws_sign ( char * const str )
{
  HMAC_CTX ctx;
  unsigned char MD[256];
  unsigned len;

  __debug("StrToSign:\n%s", str );

  HMAC_CTX_init(&ctx);
  HMAC_Init(&ctx, awsKey, strlen(awsKey), EVP_sha1());
  HMAC_Update(&ctx,(unsigned char*)str, strlen(str));
  HMAC_Final(&ctx,(unsigned char*)MD,&len);
  HMAC_CTX_cleanup(&ctx);

  char * b64 = __b64_encode (MD,len);
  __debug("Signature:  %s", b64 );

  return b64;
}
/*!
  \}
*/



#define SQS_REQ_TAIL   "&Signature=%s" "&SignatureVersion=1" "&Timestamp=%s" "&Version=2009-02-01"
/*!
  \defgroup sqs SQS Interface Functions
  \{
*/


/// Create SQS queue
/// \param b I/O buffer
/// \param name queue name
/// \return on success return 0, otherwise error code
int sqs_create_queue ( IOBuf *b, char * const name )
{
  __debug ( "Creating Que: %s\n", name );

  char  resource [1024];
  char  customSign [1024];
  char * date = NULL;
  char * signature = NULL;
  
  char * Req = 
    "http://%s/"
    "?Action=CreateQueue"
    "&QueueName=%s"
    "&AWSAccessKeyId=%s"
    SQS_REQ_TAIL ;

  char * Sign = "ActionCreateQueue"
                "AWSAccessKeyId%s"
                "QueueName%s"
                "SignatureVersion1"
                "Timestamp%sVersion2009-02-01";

  date = __aws_get_iso_date  ();
  snprintf ( customSign, sizeof(customSign), Sign, awsKeyID, name, date );
  signature =  SQSSign ( customSign );

  snprintf ( resource, sizeof(resource), SQSHost, Req , name, awsKeyID, signature, date );

  int sc = SQSRequest( b, "POST", resource ); 
  free ( signature );
  return sc;

}

/// Retrieve URL of the queue
/// \param b I/O buffer
/// \param prefix queue prefix. better use the whole name
/// \return on success return 0, otherwise error code
///
/// URL is placed into the I/O buffer. User get_line to retrieve it
int sqs_list_queues ( IOBuf *b, char * const prefix )
{
  __debug ( "Listing Queues PFX: %s\n", prefix );

  char  resource [1024];
  char  customSign [1024];
  char * date = NULL;
  char * signature = NULL;
  
  char * Req = 
    "http://%s/"
    "?Action=ListQueues"
    "&QueueNamePrefix=%s"
    "&AWSAccessKeyId=%s"
      SQS_REQ_TAIL ;

  char * Sign = "ActionListQueues"
                "AWSAccessKeyId%s"
                "QueueNamePrefix%s"
                "SignatureVersion1"
                "Timestamp%sVersion2009-02-01";

  date = __aws_get_iso_date  ();
  snprintf ( customSign, sizeof(customSign), Sign, awsKeyID, prefix, date );
  signature =  SQSSign ( customSign );

  snprintf ( resource, sizeof(resource), Req , SQSHost , prefix, awsKeyID,
             signature, date );

  IOBuf *nb = aws_iobuf_new();
  int sc = SQSRequest( nb, "POST", resource ); 
  free ( signature );

  if ( nb->result != NULL )
    b-> result = strdup(nb->result);
  b-> code   = nb->code;

  /// \todo This only retrieves just one line in the string..
  ///       make that all URLs are returned

  if ( b->code == 200 ) {
    /// Parse Out the List Of Queues
    while(-1) {
      char Ln[1024];
      aws_iobuf_getline ( nb, Ln, sizeof(Ln));
      if ( Ln[0] == 0 ) break;
      char *q = strstr ( Ln, "<QueueUrl>" );
      if ( q != 0 ) {
        q += 10;
        char * end = NULL;
        end = strstr ( q, "</QueueUrl>" );
        if ( *end != 0 ) {
          * end = 0;
          aws_iobuf_append ( b, q, strlen(q ));
          aws_iobuf_append ( b, "\n", 1 );
        }
      }
    }      
  }
  aws_iobuf_free ( nb );

  return sc;
}


/// Retrieve queue attributes
/// \param b I/O buffer
/// \param url queue url. Use sqs_list_queues to retrieve
/// \param timeOut queue visibility timeout
/// \param nMesg   approximate number of messages in the queue
/// \return on success return 0, otherwise error code
int sqs_get_queueattributes ( IOBuf *b, char * url, int *timeOut, int *nMesg )
{
  __debug ( "Getting Que Attributes\n" );

  char  resource [1024];
  char  customSign [1024];
  char * date = NULL;
  char * signature = NULL;

  char * Req = 
    "%s/"
    "?Action=GetQueueAttributes"
    "&AttributeName.1=VisibilityTimeout"
    "&AttributeName.2=ApproximateNumberOfMessages"
    "&AWSAccessKeyId=%s"
    SQS_REQ_TAIL ;

  char * Sign = 
    "ActionGetQueueAttributes"
    "AttributeName.1VisibilityTimeout"
    "AttributeName.2ApproximateNumberOfMessages"
    "AWSAccessKeyId%s"
    "SignatureVersion1"
    "Timestamp%s"
    "Version2009-02-01";

  date = __aws_get_iso_date  ();
  snprintf ( customSign, sizeof(customSign), Sign, awsKeyID, date );
  signature =  SQSSign ( customSign );

  snprintf ( resource, sizeof(resource), Req , url, awsKeyID, signature, date );

  const char *pfxVisTO = "<Name>VisibilityTimeout</Name><Value>";
  const char *pfxQLen  = "<Name>ApproximateNumberOfMessages</Name><Value>";


  int sc = SQSRequest( b, "POST", resource ); 
  while(-1) 
    {
      char Ln[1024];
      aws_iobuf_getline ( b, Ln, sizeof(Ln));
      if ( Ln[0] == 0 ) break;
      
      char *q;
      q = strstr ( Ln, pfxVisTO );
      if ( q != 0 ) { *timeOut = atoi(q+strlen(pfxVisTO));  }
      q = strstr ( Ln, pfxQLen );
      if ( q != 0 ) { *nMesg = atoi(q+strlen(pfxQLen));  }
    }

  free ( signature );
  return sc;
}

/// Set queue visibility timeout
/// \param b I/O buffer
/// \param url queue url. Use sqs_list_queues to retrieve
/// \param sec queue visibility timeout
/// \return on success return 0, otherwise error code
int sqs_set_queuevisibilitytimeout ( IOBuf *b, char * url, int sec )
{
  __debug ( "Setting Visibility Timeout : %d\n", sec );

  char  resource [1024];
  char  customSign [1024];
  char * date = NULL;
  char * signature = NULL;

  char * Req = 
    "%s/"
    "?Action=SetQueueAttributes"
    "&Attribute.1.Name=VisibilityTimeout"
    "&Attribute.1.Value=%d"
    "&AWSAccessKeyId=%s"
    SQS_REQ_TAIL ;

  char * Sign = 
    "ActionSetQueueAttributes"
    "Attribute.1.NameVisibilityTimeout"
    "Attribute.1.Value%d"
    "AWSAccessKeyId%s"
    "SignatureVersion1"
    "Timestamp%s"
    "Version2009-02-01";

  date = __aws_get_iso_date  ();
  snprintf ( customSign, sizeof(customSign), Sign, sec, awsKeyID, date );
  signature =  SQSSign ( customSign );

  snprintf ( resource, sizeof(resource), Req , 
             url, sec, awsKeyID, signature, date );

  int sc = SQSRequest( b, "POST", resource ); 
  free ( signature );
  return sc;
}

/// Send a message to the queue
/// \param b I/O buffer
/// \param url queue url. Use sqs_list_queues to retrieve
/// \param msg a message to send
/// \return on success return 0, otherwise error code
int sqs_send_message ( IOBuf *b, char * const url, char * const msg )
{
  __debug ( "Sending Message to the queue %s\n[%s]",
            url, msg );

  char  resource [10900];
  char  customSign [10900];
  char * date = NULL;
  char * signature = NULL;
  char  encodedMsg[8192];

  __aws_urlencode ( msg, encodedMsg, sizeof(encodedMsg));
  __debug ( "Encoded MSG %s", encodedMsg );

  char * Req = 
    "%s/"
    "?Action=SendMessage"
    "&MessageBody=%s"
    "&AWSAccessKeyId=%s"
    SQS_REQ_TAIL ;

  char * Sign = 
    "ActionSendMessage"
    "AWSAccessKeyId%s"
    "MessageBody%s"
    "SignatureVersion1"
    "Timestamp%s"
    "Version2009-02-01";

  date = __aws_get_iso_date  ();
  snprintf ( customSign, sizeof(customSign), Sign, awsKeyID, msg, date );
  signature =  SQSSign ( customSign );

  snprintf ( resource, sizeof(resource), Req , 
             url, encodedMsg, awsKeyID, signature, date );

  int sc = SQSRequest( b, "POST", resource ); 
  free ( signature );
  return sc;
}

/// Retrieve a message from the queue
/// \param b I/O buffer
/// \param url queue url. Use sqs_list_queues to retrieve
/// \param id Message receipt handle. 
/// \return on success return 0, otherwise error code
///
/// Message contents are placed into I/O buffer
/// Caller has to allocate enough memory for the receipt handle 
/// 1024 bytes should be enough
int sqs_get_message ( IOBuf * b, char * const url, char * id  )
{
  __debug ( "Retieving message from: %s", url );

  char  resource [10900];
  char  customSign [10900];
  char * date = NULL;
  char * signature = NULL;

  char * Req = 
    "%s/"
    "?Action=ReceiveMessage"
    "&AWSAccessKeyId=%s"
    SQS_REQ_TAIL ;

  char * Sign = 
    "ActionReceiveMessage"
    "AWSAccessKeyId%s"
    "SignatureVersion1"
    "Timestamp%s"
    "Version2009-02-01";

  date = __aws_get_iso_date  ();
  snprintf ( customSign, sizeof(customSign), Sign, awsKeyID, date );
  signature =  SQSSign ( customSign );

  snprintf ( resource, sizeof(resource), Req , 
             url, awsKeyID, signature, date );
  free ( signature );

  IOBuf * bf = aws_iobuf_new();
  int sc = SQSRequest( bf, "POST", resource ); 

  b->code = bf->code;
  b->result = strdup(bf->result);
  
  if ( bf->code != 200 ) {
    aws_iobuf_free(bf);
    return sc;
  }

  /// \todo This is really bad. Must get a real message parser
  int inBody = 0;
  while(-1) {
    char Ln[1024];
    aws_iobuf_getline ( bf, Ln, sizeof(Ln));
    if ( Ln[0] == 0 ) break;

    __debug ( "%s|%s|", inBody ? ">>": "", Ln );

    char *q;
    char *e;

    /// Handle a body already being processed..
    if ( inBody ) {
      e = strstr ( Ln, "</Body>" );
      if ( e ) { *e = 0; inBody = 0; }
      aws_iobuf_append (b, Ln, strlen(Ln));
      if ( ! inBody ) break;
      continue;     
    }

    q = strstr ( Ln, "<ReceiptHandle>" );
    if ( q != 0 ) {
      q += 15;
      e = strstr ( Ln, "</ReceiptHandle>" );
      *e = 0;
      strcpy ( id, q );
      q = e+1;
      q = strstr ( q, "<Body>" );
      if ( q != 0 ) {
        q += 6;
        e = strstr ( q, "</Body>" );
        if ( e ) *e = 0; else inBody = 1;
        aws_iobuf_append (b, q, strlen(q));
      }
    }
  }
     

  return sc;
}

/// Delete processed message from the queue
/// \param bf I/O buffer
/// \param url queue url. Use sqs_list_queues to retrieve
/// \param receipt Message receipt handle. 
/// \return on success return 0, otherwise error code
///
int sqs_delete_message ( IOBuf * bf, char * const url, char * receipt )
{
  char  resource [10900];
  char  customSign [10900];
  char * date = NULL;
  char * signature = NULL;

  char * Req = 
    "%s/"
    "?Action=DeleteMessage"
    "&ReceiptHandle=%s"
    "&AWSAccessKeyId=%s"
      SQS_REQ_TAIL ;

  char * Sign = 
    "ActionDeleteMessage"
    "AWSAccessKeyId%s"
    "ReceiptHandle%s"
    "SignatureVersion1"
    "Timestamp%s"
    "Version2009-02-01";

  date = __aws_get_iso_date  ();
  snprintf ( customSign, sizeof(customSign), Sign, awsKeyID, receipt, date );
  signature =  SQSSign ( customSign );

  char encReceipt[1024];
  __aws_urlencode ( receipt, encReceipt, sizeof(encReceipt));

  snprintf ( resource, sizeof(resource), Req , url, encReceipt, awsKeyID, signature, date );
  free ( signature );

  int sc = SQSRequest( bf, "POST", resource ); 
  return sc;
}

/*!
  \}
*/



/*!
  \defgroup iobuf I/O Buffer functions
  \{
*/

/// Create a new I/O buffer
/// \return a newly allocated I/O buffer
IOBuf * aws_iobuf_new ()
{
  IOBuf * bf = malloc(sizeof(IOBuf));
  memset(bf, 0, sizeof(IOBuf));

  return bf;
}



// (for internal use.)
//
// Walk down the list and release IOBufNodes
// Release all buffers that are non-static.
void iobuf_node_list_free(IOBufNode* n) {
   while (n) {
      if (n->buf && !n->is_static)
         free(n->buf);
     
      IOBufNode* next = n->next;
      free(n);
      n = next;
   }
}

/// Free IOBufNodes, and reset to pristine state.  This allows reuse of the
/// same IOBuf, across multiple calls.  Leave several fields intact.
void aws_iobuf_reset(IOBuf* bf) {

  /// Release local contents of the IOBuf
  if ( bf->result  ) free ( bf->result  );
  if ( bf->lastMod ) free ( bf->lastMod );
  if ( bf->eTag    ) free ( bf->eTag    );

  iobuf_node_list_free(bf->first);

  aws_metadata_reset(&bf->meta);

  // prepare to wipe the base IOBuf clean
  // (except for read_fn, write_fn, growth_size, user_data)
  HeaderFnPtr header_fn   = bf->header_fn;
  WriteFnPtr  write_fn    = bf->write_fn;
  ReadFnPtr   read_fn     = bf->read_fn;
  size_t      growth_size = bf->growth_size;
  void*       user_data   = bf->user_data;

  memset(bf, 0, sizeof(IOBuf));

  // restore fields that are not to be wiped during a reset
  bf->header_fn   = header_fn;
  bf->write_fn    = write_fn;
  bf->read_fn     = read_fn;
  bf->growth_size = growth_size;
  bf->user_data   = user_data;
}

/// Release IO Buffer, and its linked-list of IOBufNodes
/// \param  bf I/O buffer to be deleted
void   aws_iobuf_free ( IOBuf * bf )
{ 
  aws_iobuf_reset(bf);
  free (bf);
}


/// This sets the default size of new buffers, that will be allocated by
/// aws_iobuf_append functions, when they are adding data and have
/// exhausted available storage.  If <size> is zero, append functions will
/// add only the exact amount needed.  This doesn't affect the behavior of
/// the aws_iobuf_extend functions.
void aws_iobuf_growth_size (IOBuf* b, size_t size)
{
   b->growth_size = size;
}

/// Append data to I/O buffer.  This is called e.g. by writeFunc(), when
/// more data is being received from the server, or by user who is adding
/// text to be included in a PUT/POST.  We allocate extra room for a
/// terminal '0' (which we add), but we can handle binary data, as well.
///
/// NOTE: If needed, this function allocates a new buffer (IOBufNode) to
///       hold the contents of <d>, and copies <d> into there.  If you have
///       a dynamically-allocated buffer containing data you want to add,
///       you can avoid the copy by calling aws_iobuf_append_dynamic().  If
///       you have a static buffer, you can add it without copying by
///       calling aws_iobuf_append_static().
///
/// \param B     I/O buffer
/// \param d     pointer to the data to be appended
/// \param len   length of the data to be appended
///
void   aws_iobuf_append     (IOBuf* b, char* d, size_t len) {
   aws_iobuf_append_internal(b, d, len, 1, 0);
}

void   aws_iobuf_append_str (IOBuf* b, char* d) {
   aws_iobuf_append_internal(b, d, strlen(d), 1, 0);
}


/// Add a given buffer directly into the IOBuf, without copying.
///
/// (See comments at aws_iobuf_append().
///
/// WARNING: when you call this function, it is assumed you are: (a)
/// providing a buffer that was allocated with malloc(), and (b) never
/// going to call free() on that storage.
///
/// You can still call aws_iobuf_reset() and aws_iobuf_free() on the IOBuf
/// argument.
///
void   aws_iobuf_append_dynamic (IOBuf* b, char* d, size_t len) {
   aws_iobuf_append_internal(b, d, len, 0, 0);
}

/// Add a given buffer directly into the IOBuf, without copying.
///
/// (See comments at aws_iobuf_append().
///
/// WARNING: when you call this function, it is assumed you are providing a
/// buffer that will continue to be valid for the lifetime of the IOBuf
/// argument (or until you call aws_iobuf_reset()).
///
/// The buffer you give to this function will never be free'ed by any
/// aws_iobuf functions.
///
void   aws_iobuf_append_static (IOBuf* b, char* d, size_t len) {
   aws_iobuf_append_internal(b, d, len, 0, 1);
}



/// we assume that <needs_alloc> implies that you need a copy from <d> into
/// the newly-allocated buff.  If <do_copy> is false, your pointer
/// becomes the buff-pointer.  If <is_static> is non-zero, then this
/// pointer will not be free'ed by aws functions.  (This doesn't mean that
/// the storage itself must actually be static; it could be dynamic storage
/// that you just don't want to be freed, for some reason.)
///
void   aws_iobuf_append_internal (IOBuf* b,
                                  char*  d,
                                  size_t len,
                                  int    do_copy,
                                  int    is_static) {
   if (!len)
      return;

   if (do_copy) {

      // if necessary, advance into the next pre-allocated IOBufNode, or
      // create a new one.
      if (! b->writing)
         aws_iobuf_extend_dynamic(b, NULL, len);
      if (b->writing->write_count == b->writing->len) {
         if (b->writing->next)
            b->writing = b->writing->next;
         else {
            aws_iobuf_extend_dynamic(b, NULL, len);
            b->writing = b->last;
         }
      }

      // Copy user's data into the tail end of the IOBufNode chain.  If
      // there's not enough room, write the remainder into a new node, and
      // add that at the end.

      IOBufNode* wr = b->writing;
      size_t avail  = (wr->len - wr->write_count);
      size_t move   = ((len > avail) ? avail : len);
      size_t remain = ((len > avail) ? len - move : 0);

      // copy data into the unwritten portion, in the last IOBufNode
      if (move) {
         memcpy(wr->buf + wr->write_count, d, move);
         wr->write_count += move;
         b->write_count  += move;
         b->avail        += move;
      }

      // copy remaining data into a new node, added at the tail of the list
      if (remain)
         aws_iobuf_append_internal(b, d+move, remain, do_copy, is_static);
   }
   else {

      // User is giving us a buffer full of data.  Add it to the tail.
      // First, we use the "extend" functions, which treat the user's
      // buffer as empty storage, appending this buffer to the tail.  Then,
      // we adjust the write_counts, etc, to indicate that it is actually
      // full of data.
      //
      // NOTE: We ignore any unused storage that was at the tail of the
      //       list prior to the new addition.

      aws_iobuf_extend_internal(b, d, len, is_static);

      b->writing = b->last;
      b->writing->write_count = len;
      b->write_count += len;
      b->avail       += len;
  }
}


// ---------------------------------------------------------------------------
// "extend" functions.  These are for adding empty storage to an IOBuf.
// This space will receive data during future writes (e.g. during a GET).
// In order to add actual data (e.g. for a PUT), see the "append"
// functions.
// ---------------------------------------------------------------------------

void   aws_iobuf_extend_static (IOBuf* b,
                                char*  d,
                                size_t len) {

   aws_iobuf_extend_internal(b, d, len, 1);
}

// if <d> is NULL, we'll allocate the dynamic memory ourselves
void   aws_iobuf_extend_dynamic (IOBuf* b,
                                 char*  d,
                                 size_t len) {

   aws_iobuf_extend_internal(b, d, len, 0);
}

void   aws_iobuf_extend_internal (IOBuf* b,
                                  char*  d,
                                  size_t len,
                                  int    is_static) {

   // new IOBufNode, to add into <b>
   IOBufNode * N = malloc(sizeof(IOBufNode)); /* new node-ptr */

   size_t buf_size = ( ((d == NULL) && (b->growth_size > len)) ? b->growth_size : len);
   N->buf = (d ? d : malloc(buf_size)); /* your buffer belong to us */
   if (buf_size && !d && !N->buf) {
      fprintf(stderr, "aws_iobuf_extend_internal -- couldn't alloc %ld bytes\n", buf_size);
      exit(1);
   }

   N->len  = buf_size;
   N->write_count = 0;
   N->is_static = is_static;     /* is_static means aws4c fns will never call free() */
   N->next = NULL;

   // make changes to <b>
   if ( b->first == NULL ) {
      b->first       = N;
      b->last        = N;
      b->reading     = N;
      b->read_pos    = N->buf;
      b->writing     = N;
      b->len         = buf_size;
      b->write_count = 0;
   }
   else {
      b->last->next  = N;
      b->last        = N;
      b->len        += buf_size;
   }
}


/// Read the next "line" of ascii text from the buffer, or, if the buffer
/// appears to contain binary data, read up to the caller's given <size>.
/// In general, we copy buffered data up to the next newline, or to <size>,
/// whichever comes first.  If we run out of buffered data before getting to
/// either condition, then return what we've got.
///
/// NOTE: This is still imperfect for binary data, which may happen to
/// contain a '\n'.  In that case, it will return early, after reading the
/// '\n'.  You can still call repeatedly with binary data, and it will
/// correctly return results.  See aws_iobuf_get_raw(), if you know your
/// data is binary, and don't want to return anything less than <size> per
/// call, unless you're at the end.
///
///  \param B I/O buffer
///  \param Line  character array to store the read line in
///  \param size  size of the character array Line
///  \return  number of characters read or 0 

size_t aws_iobuf_getline   ( IOBuf * B, char * Line, size_t size )
{
  size_t ln = 0;
  memset ( Line, 0, size );

  if ( B->reading == NULL )
    return 0;

  // This is the "terminal 0" added by aws_iobuf_append().
  // It marks an illegal position at the end of an IOBuf.
  // This works for ascii or binary data.
  char* buf_end = B->reading->buf + B->reading->write_count;

  while ( ln < size ) {
    if ( B->read_pos == buf_end ) {
      B->reading = B->reading->next;
      if ( B->reading == NULL )
        break;
      B->read_pos = B->reading->buf;
      buf_end     = B->reading->buf + B->reading->write_count;
      continue;
    }

    char read_ch = *B->read_pos;
    Line[ln++] = *(B->read_pos++);
    if (read_ch == '\n')
       break;
  }

  B->avail -= ln;
  return ln;
}

// aws_iobuf_getline() works a little better for binary-data, now, but will
// still return early if it encounters '\n'.  This is better, if you know
// you're dealing with binary data.
//
// TBD: Should just memmove() portions of IOBufNodes, instead of this
//      careful per-character loop.
size_t aws_iobuf_get_raw   ( IOBuf * B, char * Line, size_t size )
{
  size_t ln = 0;
  ///  memset ( Line, 0, size );

  if ( B->reading == NULL )
    return 0;

  // This is the "terminal 0" added by aws_iobuf_append().
  // It marks an illegal position at the end of an IOBuf.
  // This works for ascii or binary data.
  char* buf_end = B->reading->buf + B->reading->write_count;

  while ( ln < size ) {
    if ( B->read_pos == buf_end ) {
      B->reading = B->reading->next;
      if ( B->reading == NULL )
        break;
      B->read_pos = B->reading->buf;
      buf_end     = B->reading->buf + B->reading->write_count;
      continue;
    }

    Line[ln++] = *(B->read_pos++);
  }

  B->avail -= ln;
  return ln;
}


// install (a pointer to) a custom header-function onto the IOBuf.
// This is called to parse individual header fields in a response.
void   aws_iobuf_headerfunc(IOBuf* b, HeaderFnPtr header_fn) {
   b->header_fn = header_fn;
}

// install (a pointer to) a custom read-function onto the IOBuf.
// This is called when a PUT/POST needs more data to send.
void   aws_iobuf_readfunc  (IOBuf* b, ReadFnPtr  read_fn) {
   b->read_fn = read_fn;
}

// install (a pointer to) a custom write-function onto the IOBuf.
// This is called when more data is received, during a GET.
void   aws_iobuf_writefunc (IOBuf* b, WriteFnPtr write_fn) {
   b->write_fn = write_fn;
}

// Set chunked-transfer-encoding ON / OFF
void aws_iobuf_chunked_transfer_encoding(IOBuf* b, int enable) {
   if (enable)
      b->flags |= IOBF_CTE;
   else
      b->flags &= ~(IOBF_CTE);
}


// Consolidate the contents of all internal IOBufNodes into a single
// IOBufNode.  This allows you to see the storage as a single contiguous
// area of memory.  This could be useful if you want to parse XML from a
// response received via s3_post(), but you're not sure whether there are
// more than one IOBufNodes in the response IOBuf.
//
// NOTE: aws_iobuf_get_line() and aws_iobuf_get_raw() will never read past
//      the end of the written data, so they don't have to worry about the
//      fact that we don't write a terminal NULL.  But this function is
//      giving you something you can treat as a single C string, so we have
//      to make sure there's a terminal NULL.
//
// TBD: transfer "read_pos" in the caller's IOBuf to the corresponding
//      position in the new IOBuf.  For now, after realloc, read_pos points
//      at the beginning of the data.
//
void    aws_iobuf_realloc   ( IOBuf * B )
{
  if (B->first == NULL)
    return;

  // insure there is a terminal NULL
  aws_iobuf_append(B, "", 1);

  if (B->first->next == NULL)
    return;

  // use "extend" functions, to allocate new storage
  IOBufNode* old = B->first;
  B->first = NULL;
  aws_iobuf_extend_dynamic(B, NULL, B->write_count);

  // transfer contents from old IOBufNodes into single new IOBufNode
  IOBufNode* src;
  char*      dst = B->first->buf;
  for (src=old; src; src=src->next) {
    memcpy(dst, src->buf, src->write_count);
    dst += src->write_count;
    B->first->write_count += src->write_count;
  }

  // done with the old IOBufNodes
  iobuf_node_list_free(old);

  // data and storage volumes haven't changed
  B->write_count = B->first->write_count;
}



// You could maintain a list of your own (via aws_metadata_set()), then
// install it selectively into an IOBuf.  You could also keep different
// meta-data lists you wanted to use for different objects, and install the
// one you wanted, without rebuilding it every time.
//
// NOTE: aws_iobuf_reset() will deallocate any meta-data you have added to
//       the iobuf.  If you are going to call aws_iobuf_reset(), and you
//       don't want to free and reallocate the meta-data list, you could do
//       something like this:
//
//    MetaNode* temp = iobuf->meta;
//    aws_iobuf_set_metadata(iobuf, NULL);
//    aws_iobuf_reset(iobuf);
//    aws_iobuf_set_metadata(iobuf, temp);
//
void aws_iobuf_set_metadata( IOBuf* b, MetaNode* list) {
   b->meta = list;
}



// These maintain an independent list of metadata key-value pairs.
// You can install them into an IOBuf via aws_iobuf_set_metadata().

// return the value coresponding to a given key, or return NULL.
const char* aws_metadata_get(const MetaNode** list, const char* key) {
   const MetaNode* pair;
   for (pair=*list; pair; pair=pair->next) {
      if (!strcmp(key, pair->key)) {
         return pair->value;
      }
   }
   return NULL;
}

// If key already exists, replace the value.  Otherwise, push new key/val
// onto the front of the list.  If <value> is null, then remove the entry.
void aws_metadata_set(MetaNode** list, const char* key, const char* value) {
   MetaNode* pair;
   for (pair=*list; pair; pair=pair->next) {
      if (!strcmp(key, pair->key)) {
         free(pair->value);
         pair->value = NULL;
         break;
      }
   }
   if (!pair) {
      pair        = malloc(sizeof(MetaNode));
      pair->key   = strdup(key);
      pair->value = NULL;
      pair->next  = *list;
      *list       = pair;  /* push onto front of list */
   }
   pair->value = strdup(value);
}

void aws_metadata_reset(MetaNode** list) {
   MetaNode* pair;
   MetaNode* next;
   for (pair=*list; pair; pair=next) {
      next = pair->next;
      free(pair->key);
      free(pair->value);
      free(pair);
   }
   *list=NULL;
}


/*!
  \}
*/
