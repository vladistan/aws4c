

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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>             /* strerror() */
#include <errno.h>
#include <ctype.h>              /* isprint() */

#include "aws4c.h"



void usage(int argc, char* argv[]) {

}


int main ( int argc, char * argv[] )
{

   if ((argc < 4) || (argc > 5)) {
      fprintf(stderr, "Usage:\n");
      fprintf(stderr, "  %s <host> <bucket> <object> [ <use_digest?> ]\n", argv[0]);
      fprintf(stderr, "  host should include a port, if needed\n");
      exit(EXIT_FAILURE);
   }
   const char* host     = argv[1];
   const char* bucket   = argv[2];
   const char* obj_id   = argv[3];
   int         digest_p = 0;

   if (argc >= 5) {
      errno = 0;
      digest_p = strtoull(argv[4], (char**)NULL, 10);
      if (errno) {
         fprintf(stderr, "couldn't parse '%s'\n", argv[4]);
         exit(EXIT_FAILURE);
      }
   }

   aws_init ();
   aws_set_debug ( 1 );

   char* const user = getenv("USER");
   int         rc = aws_read_config( user );
   if ( rc ) {
      fprintf(stderr, "Could not find a credential for '%s' in the config file\n",
              user);
      fprintf(stderr, "Make sure your ~/.awsAuth file is correct\n" );
      exit(EXIT_FAILURE);
   }

   s3_set_host(host);
   s3_set_bucket(bucket);
   s3_http_digest(digest_p);

   IOBuf*      bf = aws_iobuf_new ();
   int         rv;

#if 0
   // download the contents of "/<user>/test" into file "./test"
   aws_set_debug(1);
   AWS4C_CHECK( rv = s3_get2(bf, (char*)obj_id, (char*)obj_id, NULL) );

#elif 0
   // just send a HEAD request
   AWS4C_CHECK( rv = s3_head( bf, (char*)obj_id ) );

#else
   // download the contents of /<user>/test, parsing response headers into <bf>
   AWS4C_CHECK( rv = s3_get( bf, (char*)obj_id ) );

#endif

   printf ( "RV %d\n", rv );

   printf ( "CODE    [%d] \n", bf->code );
   printf ( "RESULT  [%s] \n", bf->result );
   printf ( "LEN     [%ld] \n", bf->len );
   printf ( "C-LEN   [%ld] \n", bf->contentLen );
   printf ( "LASTMOD [%s] \n", bf->lastMod );
   printf ( "ETAG    [%s] \n", bf->eTag );

   const int group_size = 4;
   const int line_size  = 32;
   char      xlate[line_size+1];
   int       xlate_pos = 0;

   int       offset = 0; /* position in total output (for binary output) */
   int       newline_offset = 0;
   int       ascii = 0;  /* object is assumed to contain nothing but ascii text? */

   xlate[line_size] = 0;
   while (1) {
      const int BUF_SIZE = 1024;
      char      Ln[BUF_SIZE];

      // read next line from response
      int       sz = aws_iobuf_getline ( bf, Ln, BUF_SIZE);

      if (! sz)
         break;

      // on very first character of response, we decide whether object is ascii or binary.
      if (! offset)
         ascii = isprint(Ln[0]);

      // different processing for ascii versus binary response
      if ( ascii )
         printf ( "S[%3d] %s", sz, Ln );
      else {


         /* quick-and-dirty impl of 'hexdump -C ...' */
         int i;
         for (i=0; i<sz; ++i) {
              
            if (offset == newline_offset)
               printf("%08x:  ", offset);
            else if (! (offset % group_size))
               printf(" ");

            printf("%02x", (unsigned int)Ln[i]);
            xlate[xlate_pos++] = (isprint(Ln[i]) ? Ln[i] : '.');

            ++ offset;

            if (! (offset % line_size)) {
               newline_offset = offset;
               printf("  |%s|\n", xlate);
               xlate_pos=0;
            }
         }
      }
   }

   // print ascii translation for incomplete final line of binary input, if needed.
   if (offset != newline_offset) {
      xlate[xlate_pos] = 0;
      printf("  |%s|\n", xlate);
   }

   aws_iobuf_free ( bf );

   return 0;
}
