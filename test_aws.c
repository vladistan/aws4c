// Unit-tests for some of the functionality in aws4c
// This especially includes new functionality

#include <assert.h>

#include <string.h>             /* strcmp() */
#include <stdlib.h>             /* exit() */
#include <unistd.h>             /* sleep() */
#include <stdio.h>
#include <time.h>               /* difftime() */
#include <sys/time.h>           /* gettimeofday(), struct timeval  */

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
void s3_connect(char* ip_addr) {
   static int connected = 0;
   if (connected)
      return;

   aws_read_config(getenv("USER"));
	aws_reuse_connections(1);

   // fix this to match your site and testing target.
	snprintf(buff, BUFF_LEN, "%s:9020", ip_addr);
	s3_set_host(buff);

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




void
usage(char* prog_name, size_t exit_code) {
   fprintf(stderr, "Usage: %s <ip_address> <test_number>\n", prog_name);
   exit(exit_code);
}


int
main(int argc, char* argv[]) {

   if (argc != 3) {
      usage(argv[0], 1);
   }
   char*             prog_name   = argv[0];
   char*             ip_addr     = argv[1];
   unsigned long int test_number = strtoul(argv[2], NULL, 10);

   IOBuf* b = aws_iobuf_new();
   s3_connect(ip_addr);

   // make sure TEST_BUCKET exists
	s3_set_bucket(TEST_BUCKET);
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


   //   aws_set_debug(1);


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


   default:
      fprintf(stderr, "%s: unrecognized test-number: %lu\n", prog_name, test_number);
      usage(prog_name, 1);
   }


   return 0;
}
