// Unit-tests for some of the functionality in aws4c
// This especially includes new functionality
//
//   make test_aws
//   ./test_aws <ip_address>[:<port] <test_number>  [ <proxy_IP_w_port> ]



#include <assert.h>

#include <string.h>             /* strcmp() */
#include <stdlib.h>             /* exit() */
#include <unistd.h>             /* sleep() */
#include <stdio.h>
#include <time.h>               /* difftime() */
#include <sys/time.h>           /* gettimeofday(), struct timeval  */
#include <ctype.h>              /* DEBUGGING: iscntrl() */

#include "aws4c.h"
#include "aws4c_extra.h"


#define  BUFF_LEN  1024
char     buff[BUFF_LEN];




// ---------------------------------------------------------------------------
// test functions
// ---------------------------------------------------------------------------

char* const TEST_BUCKET = "test";


// For tests that require communication with a server.
// We need an ip-address
void s3_connect(char* host_ip, char* proxy_ip) {
   static int connected = 0;
   if (connected)
      return;

   if (aws_read_config(getenv("USER"))) {
      // probably missing a line in ~/.awsAuth
      fprintf(stderr, "read-config for user '%s' failed\n", getenv("USER"));
      exit(1);
   }
	aws_reuse_connections(1);

   // fix this to match your site and testing target.
   snprintf(buff, BUFF_LEN, "%s", host_ip);
	s3_set_host(buff);

   if (proxy_ip)
      s3_set_proxy(proxy_ip);

   connected = 1;
}






void
test_iobuf_extend(IOBuf* b) {

#  define   BUF_LEN 64
   char   buf[BUF_LEN];

   printf("\n\n*** iobuf_extend()\n");
   printf("\n\n--- initially\n");
   debug_iobuf(b, 0, 0);

   // install empty pre-allocated storage
   aws_iobuf_extend_static(b, buf, BUF_LEN);

   printf("\n\n--- after extend_static\n");
   debug_iobuf(b, 1, 0);

   // append data via copies, filling up storage and extending into new
   // buffer.
   const char*  str32 = "*23456789_123456789_123456789_1*";
   int          i;
   for (i=1; i<=3; ++i) {
      aws_iobuf_append(b, (char*)str32, 32); /* copy text.  Should use pre-alloc storage */

      printf("\n\n--- after %d append%s of %d-byte string\n",
             i, ((i > 1) ? "s" : ""), BUF_LEN);
      debug_iobuf(b, 1, 0);
   }

   // extract contents
   size_t len = b->write_count;
   char* buff = malloc(len +1);
   assert(buff);
   aws_iobuf_get_raw(b, buff, len);
   buff[len] = 0;
   printf("\n\n--- result of 'aws_iobuf_getline_raw(B, buff, len)':\n'%s'\n",
          buff);
   
   printf("\n\n--- final state\n");
   debug_iobuf(b, 1, 0);

#  undef BUF_LEN

}


void
test_listing() {
   IOBuf* results = aws_iobuf_new();

   printf("\n-- bucket list:\n");
   get_bucket_list(results);
   while (aws_iobuf_getline(results, buff, BUFF_LEN))
      printf("  %s", buff);
   aws_iobuf_reset(results);
   
   printf("\n-- objects in bucket '%s':\n", getenv("USER"));
   get_object_list(getenv("USER"), results);
   while (aws_iobuf_getline(results, buff, BUFF_LEN))
      printf("  %s", buff);
   aws_iobuf_reset(results);

   aws_iobuf_free(results);
}


void
test_bucket_ops(IOBuf* b) {
   const char* bkt_name = "name_of_non_existant_bucket";

   printf("\n-- bucket create/delete tests:\n\n");

   // delete bucket, if it already exists.  This is just to set up the
   // "create non-existant bucket" test, below.
   s3_set_bucket((char*)bkt_name);
   AWS4C_CHECK( s3_head(b, "") );
   if ( b->code == 404 )        // "404 Not Found"
      printf("  bucket '%s' doesn't exist\n", bkt_name);
   else if (b->code == 200) {                       // "200 OK" ?
      AWS4C_CHECK( s3_delete(b, "") );
      printf("  deleted pre-existing bucket '%s'\n", bkt_name);
   }
   else {
      fprintf(stderr,
              "Unexpected return-code from HEAD req: %d -> '%s'\n",
              b->code, b->result);
      exit(1);
   }

   // create bucket
   AWS4C_CHECK( s3_put(b, "") );   /* PUT creates bucket + obj */
   printf("  created bucket '%s'\n", bkt_name);

   // delete bucket
   AWS4C_CHECK( s3_delete(b, "") );
   printf("  deleted bucket '%s'\n", bkt_name);
}


// TBD: This always gets "500 Server Error".  Note sure why.
//
// S3 multi-part upload would be the right way to take advantage of
// parallelism, to do this quickly.  We have working code to do that (see
// multi2d.c).  But this test-program is not parallel.  Just create a
// big (-ish) object.  I'll wait ...
//
// NOTE: We're assuming you already called s3_set_bucket()

void
create_large_object(IOBuf* b, char* const obj_name) {

   // skip-out early, if object already exists
   AWS4C_CHECK( s3_head(b, obj_name) );
   if ( b->code == 200 ) {        // "200 OK"
      printf("object '%s' already exists\n", obj_name);
      return;
   }

   // allocate big buffer
   const size_t BIG_SIZE = (1024 * 1024 * 128); /* 128 MB */
   char* big = (char*)malloc(BIG_SIZE);
   if (! big) {
      fprintf(stderr, "couldn't allocate %ld bytes\n", BIG_SIZE);
      exit(1);
   }

   printf("creating object '%s' with size %ld bytes\n", obj_name, BIG_SIZE);

   // install <big> as data into IOBuf
   aws_iobuf_reset(b);
   aws_iobuf_append_dynamic(b, big, BIG_SIZE); // IOBuf takes ownership of <big>

   // create object using the data in IOBuf
   ///   aws_set_debug(1);
   AWS4C_CHECK( s3_put(b, obj_name) );
   AWS4C_CHECK_OK( b );
   ///   aws_set_debug(0);

   // drop ptrs to <big>, and free it
   aws_iobuf_reset(b);          // IOBuf frees <big> buffer, too
}


// Read part of an object into <read_buf>.  <offset> is the starting byte
// within the object, and <len> is how far to read.
//
// [Adapted from the IOR S3 read code.]
void
read_byte_range(IOBuf*      b,
                char* const obj_name,
                char*       read_buf,
                size_t      offset,
                size_t      len) {

#if 0
   // create large object (if it doesn't already exist)
   // NOTE: This is now done in test_read_byte_range_multiple()
   create_large_object(b, obj_name);
#endif

   // read specific byte-range from the object
   s3_set_byte_range(offset, len);

   // For performance, we append <data_ptr> directly into the linked
   // list of data in param->io_buf.  In this case (i.e. reading),
   // we're "extending" rather than "appending".  That means the
   // buffer represents empty storage, which will be filled by the
   // libcurl writefunction, invoked via aws4c.

   aws_iobuf_reset(b);
   aws_iobuf_extend_static(b, read_buf, len);
   AWS4C_CHECK   ( s3_get(b, obj_name) );

   // AWS4C_CHECK_OK( b );
   if (( b->code != 200) && ( b->code != 206)) { /* 206: Partial Content */
      fprintf(stderr, "failed to read byte-range %ld-%ld from '%s'\n",
              offset, offset+len, obj_name);
      exit(1);
   }

   // drop ptrs to <data_ptr>, in param->io_buf 
   aws_iobuf_reset(b);
}

// return microseconds difference (<b> - <a>)
static const size_t MILLION = 1000 * 1000;
size_t
udifftime(struct timeval b, struct timeval a) {
   size_t usec = ((( b.tv_sec  - a.tv_sec) * MILLION)
                  + (b.tv_usec - a.tv_usec));

   if (b.tv_usec < a.tv_usec)
      usec += MILLION;

   return usec;
}

// call read_byte_range() with a sliding series of offsets to see if there
// is different performance at different alignments.
//
void test_read_byte_range_multiple(IOBuf* b) {
   const size_t    READ_SIZE = 64 * 1024;
   char            buff[READ_SIZE];
   struct timeval  tv0, tv1;

#if 1
   // pre-allocate object, so first loop-iteration doesn't have extra cost
   // NOTE: This is failing, with filename = "big", for some reason.
   char* const obj_name = "big3";
   create_large_object(b, obj_name);
#else
   // Alternative approach, use a giant file created by the IOR tests.
   // Only trouble is that it lives in a different bucket.
   s3_set_bucket("ior");
   char* const obj_name = "experiment__2014_10_23__14_53_05.out"; /* 512 MB */
#endif

   // read partial object contents from various offsets.
   int       i, j;
   size_t    offset = 0;
   const int OFFSETS = 8;
   const int ITERATIONS = 500;

   for (i=0; i<OFFSETS+1; ++i) {
      assert(! gettimeofday(&tv0, NULL)); /* start time */

      printf("%d reads of %ld bytes at offset %6ld, ",
             ITERATIONS, READ_SIZE, offset);
      fflush(stdout);

      for (j=0; j<ITERATIONS; ++j)
         read_byte_range(b, obj_name, buff, offset, READ_SIZE);

      assert(! gettimeofday(&tv1, NULL)); /* end time */

      // compute timing and BW
      float avg_time = ((float)udifftime(tv1, tv0) / ITERATIONS) / MILLION;  /* seconds */
      float avg_BW   = (READ_SIZE * ITERATIONS / (1024 * 1024)) / avg_time;  /* MB/s */
      printf("avg time = %4.2f s, avg BW = %7.2f MiB/s\n", avg_time, avg_BW);

      offset += READ_SIZE / OFFSETS;
   }

#if 0
#else
   // resume using regular bucket, for other tests.
   s3_set_bucket(TEST_BUCKET);
#endif
}


// EMC supports appending to an object, using a byte-range header like this:
//
//    Range: bytes=-1-

char* const append_test_obj_name = "append_test14";

void test_emc_append(IOBuf* b) {


   // Need this, to prevent s3_do_put_or_post() from complaining
   s3_enable_EMC_extensions(1);
   //   aws_set_debug(1);

   // create zero-length object
   printf("creating zero-length object '%s'\n", append_test_obj_name);
   aws_iobuf_reset(b);
   s3_put(b, append_test_obj_name);

   // first write to the object
   printf("first  write to '%s'\n", append_test_obj_name);
   aws_iobuf_reset(b);
   s3_set_byte_range(-1, -1);
   aws_iobuf_append_str(b, "first");
   s3_put(b, append_test_obj_name);

   // second write to the object
   printf("second write to '%s'\n", append_test_obj_name);
   aws_iobuf_reset(b);
   aws_iobuf_append_str(b, " second");
   s3_set_byte_range(-1, -1);
   s3_put(b, append_test_obj_name);
   // aws_set_debug(0);

   // get object contents
   printf("reading from '%s'\n", append_test_obj_name);
   const size_t BUFF_SIZE = 32;
   char         buff[BUFF_SIZE +1];

   aws_iobuf_reset(b);
   s3_get(b, append_test_obj_name);

   printf("response is %d %s\n", b->code, b->result);

   int sz;
   for (sz = aws_iobuf_getline(b, buff, BUFF_SIZE);
        sz;
        sz = aws_iobuf_getline(b, buff, BUFF_SIZE)) {
      buff[sz] = 0;
      printf("readback[%d]: '%s'\n", sz, buff);
   }
}




char* const write_range_obj_name = "write_range_test";

void test_emc_write_range(IOBuf* b) {


   // Need this, to prevent s3_do_put_or_post() from complaining
   s3_enable_EMC_extensions(1);

   // create zero-length object
   printf("creating zero-length object '%s'\n", write_range_obj_name);
   aws_iobuf_reset(b);
   s3_put(b, write_range_obj_name);

   // write byte-ranges from 0 to <size>, where size is powers-of-two up to 32M
   int start = 0;
   int size;
   for (size=1; size<=(1<<25); size<<=1) {

      printf("writing range %d-%d to '%s'\n", start, start+size, write_range_obj_name);


      // allocate storage used to write to object
      char* buf = (char*)malloc(size);
      if (! buf) {
         fprintf(stderr, "Allocation of %d bytes failed\n", size);
         exit(1);
      }

      // install into IOBuf
      aws_iobuf_reset(b);
      aws_iobuf_append_dynamic(b, buf, size);

      // write the data to a byte-range
      s3_set_byte_range(start, size); /* 1-based ? */
      AWS4C_CHECK   ( s3_put(b, write_range_obj_name) );
      AWS4C_CHECK_OK( b );

      // show any response contents
      printf("response is %d %s\n", b->code, b->result);

      const size_t BUFF_SIZE = 32;
      char         buff[BUFF_SIZE +1];
      int sz;
      for (sz = aws_iobuf_getline(b, buff, BUFF_SIZE);
           sz;
           sz = aws_iobuf_getline(b, buff, BUFF_SIZE)) {
         buff[sz] = 0;
         printf("readback[%d]: '%s'\n", sz, buff);
      }

      // free dynamic storage, and clean up IOBuf
      aws_iobuf_reset(b);
      printf("\n");
   }
}


// We've added some features to allow storage of user-metadata on objects.
// You manipulate a list of key/value pairs (adding and removing pairs),
// then attach it to an iobuf that you use to write the object.  When
// reading, these metadata are captured into a list on the IOBuf, where you
// can look at them.
void test_metadata(IOBuf* b) {
   const char* obj = "test_metadata";

   // delete any old copies of the object
   printf("deleting %s ... ", obj);
   AWS4C_CHECK( s3_delete(b, (char*)obj) );
   printf("%s\n", b->result);

   // write new emtpy object, with metadata
   MetaNode* meta = NULL;
   aws_metadata_set(&meta, "foo", "1");
   aws_metadata_set(&meta, "bar", "2");
   aws_metadata_set(&meta, "foo", "3"); /* replaces the first one */

   aws_iobuf_reset(b);          /* assure no previous metadata */
   aws_iobuf_set_metadata(b, meta);
   
   AWS4C_CHECK   ( s3_put(b, (char*)obj) ); /* create empty object with user metadata */
   AWS4C_CHECK_OK( b );

   // retrieve metadata from new object
   aws_iobuf_reset(b);          /* drop all current metadata */

   AWS4C_CHECK   ( s3_get(b, (char*)obj) ); /* get object plus metadata */
   AWS4C_CHECK_OK( b );

   for (meta=b->meta; meta; meta=meta->next) {
      printf("\t%5s -> %s\n", meta->key, meta->value);
   }
}




#ifdef TEST_AWS_PTHREADS
// ...........................................................................
// Experiment with streaming PUT, where two threads work together to
// incrementally add data into a PUT.
//
// This code is only included if your define TEST_AWS_PTHREADS (at the top
// of this file).  If building with pthreads causes you some trouble, you
// can leave it undefined, and this test won't be included.
// ...........................................................................


// This custom readfunc shows how a streaming operation could be implemented.
// We block on a semaphore until data is available then provide it to the
// ongoing PUT.
#include <semaphore.h>
#include <pthread.h>
sem_t  sem1;
sem_t  sem2;

typedef struct ContextBuf {
   void*  ptr;
   size_t size;
} ContextBuf;

// double-buffered, so producer/consumer can work in parallel
typedef struct Context {
   //   ContextBuf buf[2];
   //   int        read_pos;
   ContextBuf buf;              // Nevermind.  Single-buffered.
} Context;



// ---------------------------------------------------------------------------
//
// (11) test-function inserts a Context object (instead of a buffer) into
// the IOBuf using aws_iobuf_append().  Test-function also inserts a custom
// readfunc.  Then it starts a PUT.  When curl makes a callback to the new
// readfunc, the readfunc stores a pointer to that buffer into the context,
// and unlocks the producer, then blocks waiting for the producer.  When
// producer has filled the buffer (provided by the curl callback) it
// unlocks the consumer (i.e. the freadfunc), and blocks waiting for more
// work.  Producer consumer ping-pong like this, streaming data into the
// PUT.
//
// ---------------------------------------------------------------------------
//
// *** TBD
// variation that uses Chunked Transfer-Encoding, so that the total size
// doesn't have to be known ahead of time.
size_t streaming_readfunc_cte (void* ptr, size_t size, size_t nmemb, void* stream) {
   IOBuf*   b     = (IOBuf*)stream;
   Context* ctx   = (Context*)b->writing->buf;
   size_t   total = (size * nmemb);

   // producer is waiting for context
   ctx->buf.ptr  = ptr;
   ctx->buf.size = total;
   fprintf(stderr, "\nconsumer requesting %ld\n", total); // simulate doing some work
   sem_post(&sem1);             // let producer fill our buffer

   fprintf(stderr, "consumer waiting\n"); // simulate doing some work
   sem_wait(&sem2);             // wait for producer to finish

   fprintf(stderr, "consumer wrote %ld\n", ctx->buf.size);
   return (ctx->buf.size);
}



// Curl is telling us how much data it can handle, but we can supply less
// (as long as it's not zero -- zero means we're reporting EOF).  Return
// the amount we actually supplied.
size_t streaming_readfunc (void* ptr, size_t size, size_t nmemb, void* stream) {
   IOBuf*   b     = (IOBuf*)stream;
   Context* ctx   = (Context*)b->writing->buf;
   size_t   total = (size * nmemb);

   // producer is waiting for context
   ctx->buf.ptr  = ptr;
   ctx->buf.size = total;
   fprintf(stderr, "\nconsumer requesting %ld\n", total); // simulate doing some work
   sem_post(&sem1);             // let producer fill our buffer

   fprintf(stderr, "consumer waiting\n"); // simulate doing some work
   sem_wait(&sem2);             // wait for producer to finish

   fprintf(stderr, "consumer wrote %ld\n", ctx->buf.size);
   return (ctx->buf.size);
}
// this guy provides the data used by streaming_readfunc()
void* producer_thread(void* arg) {
   static const char*   buf      = (const char*)"This is some text\n";
   static       size_t  buf_size = 0;

   if (! buf_size)
      buf_size = strlen(buf);
   Context*             ctx      = (Context*)arg;

   int    i;
   for (i=0; i<8; ++i) {

      fprintf(stderr, "producer waiting\n"); // simulate doing some work
      sem_wait(&sem1);          // wait for consumer to give us a buffer

      size_t req_size  = ctx->buf.size;
      size_t move_size = ((ctx->buf.size < buf_size) ? ctx->buf.size : buf_size);
      fprintf(stderr, "producer supplying %ld / %ld\n", move_size, req_size);
      memmove(ctx->buf.ptr, buf, move_size);
      ctx->buf.size = move_size; // show consumer how much we supplied
      // sleep(2);                 // simulate e.g. file latency

      fprintf(stderr, "producer wrote data\n"); // pretend we filled <buf>
      sem_post(&sem2);
   }

   // let consumer know there's no more data coming
   fprintf(stderr, "producer waiting (final)\n"); // simulate doing some work
   sem_wait(&sem1);          // wait for consumer to give us a buffer

   fprintf(stderr, "producer supplying 0\n"); // bupkis
   ctx->buf.size = 0;
   sem_post(&sem2);

   return NULL;
}

// We have separate threads for a producer and a consumer.  The consumer
// (i.e. streaming_readfunc) is adding data into a PUT stream.  The
// producer (i.e. producer_thread) is getting more data for the consumer.
// (Variations on this theme could use Chunked Trasfer-Encoding, if the
// final size of the object is unknown, or could provide the ultimate size
// of the object in the initial header.)
void test_streaming_write(IOBuf* b) {
   const char* obj = "streaming_write";

   // delete any old copies of the object
   printf("deleting %s ... ", obj);
   AWS4C_CHECK( s3_delete(b, (char*)obj) );
   printf("%s\n", b->result);

   // create Context;
   //   Context* ctx = (Context*)malloc(sizeof(Context));
   Context ctx;

   // NOTE: libaws4c will pass a pointer-to-the-IOBuf to
   //       readfunc/writefunc.  We "cheat" by stashing a Context instead
   //       of an actual buffer.  This is okay because our
   //       streaming_readfunc() expects this.  Either of the append/extend
   //       forms of aws_*_iobuf() is safe: libaws4c doesn't touch the
   //       contents of the user-buffer.  However, for a PUT, aws4c will
   //       supply IOBuf.write_count as the length.  "extend" is for reads
   //       so it doesn't change the IOBuf write_count but "append" is for
   //       writes, and it will give PUT whatever length we provide.  The
   //       append-call should also use the "static" form, so that
   //       aws_iobuf_reset() won't call free() on it.
   //
   // NOTE: The length we supply here has to match the amount the producer
   //       will actually supply.  (i.e. "8*18" matches the producer-thread
   //       doing 8 iterations, supplying 18 characters per iteration.
   //
   //       The alternative is to use Chunked Transfer-Encoding (CTE), and
   //       have the consumer add each string with a CTE header.
   // 
   aws_iobuf_reset(b);          // make sure context is the only thing
   aws_iobuf_append_static(b, (char*)&ctx, 8*18); // "cheating" (see NOTE above)
   aws_iobuf_readfunc(b, &streaming_readfunc);

   // start threading
   sem_init(&sem1, 0, 0);
   sem_init(&sem2, 0, 0);
   pthread_t prod;
   pthread_create(&prod, NULL, &producer_thread, &ctx);

   // let producer/consumer work together to keep data flowwing into the
   // PUT, using incremental additions.
   AWS4C_CHECK   ( s3_put(b, (char*)obj) ); /* create empty object with user metadata */
   AWS4C_CHECK_OK( b );

}



// ---------------------------------------------------------------------------
//
// (12) In this case, the producer (modeling fuse write) supplies the
// buffer via a simple aws_iobuf_append_static(), and the consumer (curl
// readfunc) repeatedly reads from it, using the normal aws_iobuf_get_raw()
// calls.  This approach makes more sense, in the case of fuse, because the
// producer's buffer may be significantly larger than the 16k that is a
// typical max for curl). This approach also turns out to be a lot simpler,
// semantically.  Plus, we don't need a Context object.
//
// To be more like the fuse-write case, we're also using chunked
// transfer-encoding.  That means each write has to include a CTE "header",
// and CTE "footer".  However, curl takes care of this for us.  If curl
// sees the "Transfer-coding: chunked" header (see
// aws_iobuf_chunked_transfer_encoding), then the callbacks here are given
// size 16372, which is 16k-12.  16k is the default max for curl buff-size,
// and 12 is the total size of CTE header + footer.
//
// We also set up the caller to do double-buffering.  That allows the
// producer to fill one buffer, while our readfunc is copying the other one
// to curl.  However, the consumer (i.e. our readfunc) must now be able to
// distinguish "end of IOBuf" from "end of data".  We could do this ad-hoc
// by having a static variable that we set when we get end-of-buff, and
// treating 2 EOBs in a row as EOF.  Instead, I've added a flag to the
// IOBuf, which the producer can set for the case of EOF.
//
// sem1 means "IOBuf drained, readfunc done with it"
// sem2 means "IOBuf filled, producer done with it"
//
// ---------------------------------------------------------------------------

size_t streaming_readfunc2 (void* ptr, size_t size, size_t nmemb, void* stream) {
   IOBuf*   b     = (IOBuf*)stream;
   size_t   total = (size * nmemb);
   fprintf(stderr, "\n--- consumer buff-size %ld\n", total);

   sem_wait(&sem2);             // wait for producer to fill buffers
   fprintf(stderr, "--- consumer got %ld\n", b->write_count);

   if (b->write_count == 0)
      return 0;                 // EOF

   // move producer's data into curl buffers.
   // (Might take more than one callback)
   size_t move_req = ((size <= b->write_count) ? size : b->write_count);
   size_t moved    = aws_iobuf_get_raw(b, (char*)ptr, move_req);
   fprintf(stderr, "--- consumer wrote %ld / %ld\n", moved, move_req);

   if (moved < b->write_count)
      sem_post(&sem2);          // next callback is pre-approved
   else
      sem_post(&sem1);          // tell producer that buffer is used

   return moved;
}
// Q: Does curl construct the CTE header/tailer for us?
//
// A: YES!  Curl calls our callback with size*nmemb == 16372.  This is
//    16k-12, so curl is clearly reserving space for the CTE header and
//    tailer (12 bytes total), and filling them in for us!
//
// NOTE: IOBuf does not currently provide infomation about how much unread
//       data is available.  Even if it did, it's still possible that
//       aws_iobuf_get_raw() would theoretically return less than that.
//       So, we write the CTE header *after* we've written the contents.
size_t streaming_readfunc2b_cte (void* ptr, size_t size, size_t nmemb, void* stream) {
   IOBuf*   b     = (IOBuf*)stream;
   size_t   total = (size * nmemb);
   fprintf(stderr, "\n--- consumer curl buff %ld\n", total);

   // wait for producer to fill buffers
   sem_wait(&sem2);
   fprintf(stderr, "--- consumer avail-data: %ld\n", b->avail);

   if (b->write_count == 0) {
      fprintf(stderr, "--- consumer got EOF\n");
      sem_post(&sem1);
      return 0;
   }

   // move producer's data into curl buffers.
   // (Might take more than one callback)
   size_t move_req = ((total <= b->avail) ? total : b->avail);
   size_t moved    = aws_iobuf_get_raw(b, (char*)ptr, move_req);

   if (b->avail) {
      fprintf(stderr, "--- consumer iterating\n");
      sem_post(&sem2);          // next callback is pre-approved
   }
   else {
      fprintf(stderr, "--- consumer done with buffer (?)\n");
      sem_post(&sem1);          // tell producer that buffer is used
   }

   return moved;
}

// Makes more sense for the producer to provide the buffer for Context,
// rather than the consumer.  (See comments at streaming_read()).  We
// allocate enough for 8 iterations, where each iteration should supply
// enough for two callbacks to streaming_readfunc2().
//
// TBD: double-buffering actually fairly easy, this way.  Producer would
// just have two buffers, and would trade off aws_iobuf_append_static()
// calls with them.  However, this means we must allow the client to
// distinguish between end-of-current-buffer, and no-more-buffer-data.
// We do that as follows:
//  buffer

void* producer_thread2(void* arg) {
   static const size_t  BUF_SIZE = (16*1024*2*8);
   static       char*   buf[2];

   // double-buffering
   buf[0] = (char*)malloc(BUF_SIZE);
   buf[1] = (char*)malloc(BUF_SIZE);
   if (!buf[0] || !buf[1]) {
      fprintf(stderr, "--- producer malloc failed (%ld bytes)\n", BUF_SIZE);
      exit(1);
   }
   int    curr = 0;             // which buf[] can we write?

   IOBuf* b = (IOBuf*)arg;
   int    i;
   for (i=0; i<8; ++i) {

      // initialize data
      fprintf(stderr, "--- producer initializing %ld bytes, in buff[%d]\n", BUF_SIZE, curr);
      memset(buf[curr], '0'+i, BUF_SIZE-1);
      buf[curr][BUF_SIZE -1] = 0;

      fprintf(stderr, "--- producer waiting for IOBuf\n"); // readfunc done with IOBuf?
      sem_wait(&sem1);

      // install buffer into IOBuf
      aws_iobuf_reset(b);
      aws_iobuf_append_static(b, buf[curr], BUF_SIZE); // "static" so iobuf_reset() won't free()
      fprintf(stderr, "--- producer appended data for consumer\n");

      // let readfunc move data
      sem_post(&sem2);
      curr ^= 1;                // toggle <curr>, so we can work on unused buff
   }

   // signal EOF to consumer
   fprintf(stderr, "--- producer indicating EOF\n");
   sem_wait(&sem1);
   aws_iobuf_reset(b);
   sem_post(&sem2);

   // wait for consumer
   sem_wait(&sem1);

   free(buf[0]);
   free(buf[1]);

   return NULL;
}

void test_streaming_write2(IOBuf* b) {
   const char* obj = "streaming_write2";

   // delete any old copies of the object
   printf("deleting %s ... ", obj);
   AWS4C_CHECK( s3_delete(b, (char*)obj) );
   printf("%s\n", b->result);

   aws_iobuf_reset(b);
   s3_chunked_transfer_encoding(1);
   ///   aws_iobuf_readfunc(b, &streaming_readfunc2_cte);
   aws_iobuf_readfunc(b, &streaming_readfunc2b_cte);

   // start threading
   sem_init(&sem1, 0, 1);       // let producer start
   sem_init(&sem2, 0, 0);
   pthread_t prod;
   pthread_create(&prod, NULL, &producer_thread2, b);

   // let producer/consumer work together to keep data flowwing into the
   // PUT, using incremental additions.
   AWS4C_CHECK   ( s3_put(b, (char*)obj) ); /* create empty object with user metadata */
   AWS4C_CHECK_OK( b );

   s3_chunked_transfer_encoding(0);
}



#else // TEST_AWS_PTHREADS not defined

void test_streaming_write(IOBuf* b) {
   fprintf(stderr, "This test is not implemented\n");
   // fprintf(stderr, "#define TEST_AWS_PTHREADS, then run 'make test_aws' again\n");
   fprintf(stderr, "run 'make clean', then 'make PTHREADS=1'\n");
   exit(1);
}
void test_streaming_write2(IOBuf* b) {
   fprintf(stderr, "This test is not implemented\n");
   // fprintf(stderr, "#define TEST_AWS_PTHREADS, then run 'make test_aws' again\n");
   fprintf(stderr, "run 'make clean', then 'make PTHREADS=1'\n");
   exit(1);
}
#endif // TEST_AWS_PTHREADS 





// At LANL, test this against 10.135.0.21:81
//
// NOTE: for the case of by-path access, the "bucket" (i.e. first element
//     of the object-ID) is really the fastcgi identifier, which in our
//     case must be "proxy".  And the second element of the second element
//     of the object-ID selects the access method, which for now must be
//     "bparc".
//
//     So, the object-ID looks like "/proxy/bparc/whatever_else_you_want"

void test_sproxyd_by_path(IOBuf* b) {
   const char* bkt = "proxy";   // not really an S3 bucket
   char        obj[256];

   // format the current time to create a (per-second) unique object-ID.
   time_t      epoch = time(NULL);
   struct tm*  local = localtime(&epoch);
   char        time_str[128];
   strftime(time_str, sizeof(time_str), "%Y%m%d_%H%M%S", local);
   snprintf(obj, sizeof(obj), "jti/sproxyd_test_%s", time_str);

   aws_set_debug(1);

   s3_enable_Scality_extensions(1);
   s3_sproxyd(1);

   s3_set_bucket(bkt);
   s3_chunked_transfer_encoding(1);

   const char* contents = "Here's a bunch of text\n";
   aws_iobuf_reset(b);
   aws_iobuf_append_static(b, (char*)contents, strlen(contents));

   printf("writing /%s/%s\n", bkt, obj);
   s3_put(b, (char*)obj);

   printf("reading /%s/%s\n", bkt, obj);
   aws_iobuf_reset(b);
   s3_get(b, (char*)obj);

   // show contents of IOBuf
   debug_iobuf(b, 1, 1);

   char read_buf[256];          // big enough to hold <contents>
   size_t read_ct = aws_iobuf_getline(b, read_buf, strlen(contents) +1);
   printf("results (%ld): %s", read_ct, read_buf);
}





void
usage(char* prog_name, size_t exit_code) {
   fprintf(stderr, "Usage: %s <ip_address> <test_number> [ <proxy_ip_w_optional_port> ]\n",
           prog_name);
   fprintf(stderr, "\n");
   fprintf(stderr, "Specify <_ip_address> as xx.xx.xx.xx:port, if you need a port\n");
   exit(exit_code);
}


int
main(int argc, char* argv[]) {

   if ((argc < 3) || (argc > 4)) {
      usage(argv[0], 1);
   }

   char*             prog_name   = argv[0];
   char*             host_ip     = argv[1];
   unsigned long int test_number = strtoul(argv[2], NULL, 10);
   char*             proxy_ip    = ((argc > 3) ? argv[3] : NULL);

   aws_init();
   s3_connect(host_ip, proxy_ip);
   s3_set_bucket(TEST_BUCKET);

   IOBuf* b = aws_iobuf_new();


   if (test_number < 100) {

      // make sure TEST_BUCKET exists
      AWS4C_CHECK( s3_head(b, "") );
      if (b->code == 404 ) {              /* 404 Not Found */
         AWS4C_CHECK   ( s3_put(b, "") ); /* creates URL as bucket + obj */
         AWS4C_CHECK_OK( b );
         printf("created bucket '%s'\n", TEST_BUCKET);
      }
      else if (b->code != 200 ) {         /* 200 OK */
         fprintf(stderr, "Unexpected HTTP return-code %d '%s'\n", b->code, b->result);
         exit(1);
      }
   }




   printf("\nbeginning test %lu\n", test_number);
   switch (test_number) {

   case 1:
      // --- Q: size_t is the same as IOR_Offset_t (long long int), on this machine?
      //     A: Yes.  Both are 8 bytes.

      printf("size_t        = %lu bytes\n"
             "long long int = %lu bytes\n",
             sizeof(size_t), sizeof(long long int));
      return 0;


   case 2:
      // --- use EMC extensions to write byte-ranges of increasing sizes
      //
      //     status: WORKS

      test_emc_write_range(b);
      return 0;


   case 3:
      // --- use EMC extensions to append to an object
      //
      //     status: WORKS

      test_emc_append(b);
      return 0;


   case 4: {
      // --- user reported a seg-fault, using aws_iobuf_append_dynamic()
      //
      //     status: user was trying to use the buffer after freeing

      const char*  curr_item = "test_2014_11_17";
      const size_t write_bytes = 1024;
      char* write_buffer = malloc(write_bytes);
      assert(write_buffer);
      int i;

      //   // this would be wrong:
      //   char* write_buffer = malloc(write_bytes);
      //   assert(write_buffer);

      for (i=0; i<4; ++i) {
         printf("iteration %d\n", i);
         b = aws_iobuf_new();

         // this is right
         char* write_buffer = malloc(write_bytes);
         assert(write_buffer);

         aws_iobuf_append_dynamic(b, write_buffer, write_bytes);
         AWS4C_CHECK( s3_put( b, (char*)curr_item ) );
         AWS4C_CHECK_OK( b );
         aws_iobuf_free ( b );
      }
      printf("looks okay, now.\n");
      return 0;
   }


   case 5: {
      // --- simpler way, with static data
      //
      //     status: WORKS

      const char*  curr_item = "test_2014_11_17";
      const char*  write_buffer = "######";
      const size_t write_bytes = strlen(write_buffer) +1; /* write final 0, too */

      int i;
      for (i=0; i<4; ++i) {
         printf("iteration %d\n", i);
         aws_iobuf_reset( b );
         aws_iobuf_append_static(b, (char*)write_buffer, write_bytes);
         AWS4C_CHECK( s3_put( b, (char*)curr_item ) );
         AWS4C_CHECK_OK( b );
      }
      printf("looks okay.\n");
      return 0;
   }


   case 6:
      // --- list buckets and objects
      //
      //     status:  WORKS

      aws_iobuf_reset(b);
      test_listing();
      return 0;


   case 7:
      // --- create a bucket
      //
      //     status: WORKS

      aws_iobuf_reset(b);
      test_bucket_ops(b);
      return 0;


   case 8:
      // --- create an IOBuf, then append until it requires more allocation
      //
      //     status: WORKS

      aws_iobuf_reset(b);
      test_iobuf_extend(b);
      return 0;


   case 9:
      // --- create an IOBuf, then append until it requires more allocation
      //     This time, use a non-default growth-size
      //
      //     status: WORKS

      aws_iobuf_reset(b);
      aws_iobuf_growth_size(b, 1000);
      test_iobuf_extend(b);
      return 0;


   case 10:
      // --- manipulate a list of user meta-data, store on an object, read
      //     back and print.
      //
      //     status: WORKS

      aws_set_debug(1);
      aws_iobuf_reset(b);
      test_metadata(b);
      return 0;


   case 11:
      // --- do a streaming write, by using a custom readfunc.
      //     The readfunc blocks until more data is available.
      //
      //     status: WORKS

      aws_set_debug(1);
      aws_iobuf_reset(b);
      test_streaming_write(b);
      return 0;


   case 12:
      // --- do a streaming write, by using a custom readfunc.
      //     The readfunc blocks until more data is available.
      //
      //     status: 

      aws_set_debug(1);
      aws_iobuf_reset(b);
      test_streaming_write2(b);
      return 0;




      // ----------------------------------------------------------------------
      // if (test_number >= 100) we will not try to assure a bucket has been
      // created.  That's because these tests might not use buckets.  For
      // example, Scality's sproxyd has no notion of "buckets".  It's just
      // pure GET/PUT/DELETE.  However, object-names will still be bucket+object
      // ----------------------------------------------------------------------

   case 100:
      // --- Use Scality sproxyd.  Currently this amounts to nothing more
      //     than suppressing the generation and installation of the
      //     "Authorization" header, in all GET/PUT/POST/DELETE requests.
      //
      //     status: WORKS

      test_sproxyd_by_path(b);
      return 0;


   case 101:
      // --- do PUT/GET/DEL, using HTTP-digest user/pass authentication
      s3_http_digest(1);
      test_sproxyd_by_path(b);
      return 0;


   default:
      fprintf(stderr, "%s: unrecognized test-number: %lu\n", prog_name, test_number);
      usage(prog_name, 1);
   }


   return 0;
}
