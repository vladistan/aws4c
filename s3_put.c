
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


// -----------------------------------
// METHOD should be set to one of these
// -----------------------------------
#define PUT_DATA  0
#define PUT_RRS   1
#define PUT_FILE  2
#define POST      3             /* init for multi-part-upload */

#define METHOD    PUT_FILE



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aws4c.h"


int putObject ( char * name, IOBuf * bf )
{
  int i;
  for ( i = 0 ; i < 9000 ; i ++ ) {
    char S[128];
    snprintf ( S,sizeof(S), "Ln %d \n" , i );
    aws_iobuf_append ( bf,S, strlen(S));
  }
  return s3_put ( bf, name );
}

int main ( int argc, char * argv[] )
{
  if (argc != 2) {
     fprintf(stderr, "Usage: %s <ip_addr>\n"
             "\n"
             "  (NOTE: <ip_addr> may include port, e.g. xx.xx.xx.xx:9020)\n",
             argv[0]);
     exit(1);
  }
  char* const ip_addr = argv[1];
  int         rv;

  aws_init();
  aws_set_debug(1);

  char* const user = getenv("USER");
  int         rc = aws_read_config(user);
  if (rc) {
    printf("Could not find a credential for '%s' in the config file\n", user);
    printf("Make sure your ~/.awsAuth file is correct\n" );
    exit (1);
  }

  s3_set_host ( ip_addr );
  s3_set_bucket (user);

  // s3_set_mime ("text/plain");
  // s3_set_mime ("application/octet-stream");
  // s3_set_acl ("public-read");

  IOBuf * bf       = aws_iobuf_new ();


#if METHOD == POST

  // Send a POST request.  The "?uploads" initiates a multi-part upload.
  // The response should be XML that contains an "UploadId", which would be
  // used in subsequent PUTs, when uploading the parts.  We capture this
  // response and print it out.  However, it could also be parsed with an
  // XML parser, such as libxml2.
  IOBuf * response = aws_iobuf_new ();

  rv = s3_post ( bf, "test?uploads");

#elif (METHOD == PUT_DATA)

  // putObject appends raw-data onto <bf>, the same IOBuf holding header
  // info.  This data will all be sent as part of the request.
  rv = putObject ( "test", bf );

#elif (METHOD == PUT_FILE)

  // PUT contents of file "upload_me" to the object named "test", under a
  // bucket with same name as the name of the user.  headers go to <bf>
  rv =  s3_put2 (bf, "test", "upload_me", NULL );

#else

  printf("METHOD must be #define'd to be PUT_DATA, PUT_FILE, or POST");
  exit(1);

#endif


  printf ( "RV %d\n", rv );

  // show headers received from server
  printf ( "CODE    [%d] \n", bf->code );
  printf ( "RESULT  [%s] \n", bf->result );
  printf ( "LEN     [%ld] \n", bf->len );
  printf ( "LASTMOD [%s] \n", bf->lastMod );
  printf ( "ETAG    [%s] \n", bf->eTag );

  while (1) {
    char Ln[128]; // Ln[1024];
    int sz = aws_iobuf_getline ( bf, Ln, sizeof(Ln));
    printf ( "S[%3d] ", sz );
    if ( Ln[0] == 0 ) {
      printf("\n");
      break;
    }
    printf ( "%s", Ln );
  }

#if (METHOD == POST)
  // show headers in the <response>
  printf ( "response->CODE    [%d] \n", response->code );
  printf ( "response->RESULT  [%s] \n", response->result );
  printf ( "response->LEN     [%ld] \n", response->len );
  printf ( "response->LASTMOD [%s] \n", response->lastMod );
  printf ( "response->ETAG    [%s] \n", response->eTag );

  // show the XML in the response
  while (1) {
    char Ln[128]; // Ln[1024];
    int sz = aws_iobuf_getline ( response, Ln, sizeof(Ln));
    printf ( "response[%3d] ", sz );
    if ( !sz ) {
      printf("\n");
      break;
    }
    printf ( "%s", Ln );
    if (sz == 128)
      printf("\n");
  }

#endif

  // DEBUGGING
  exit(0);

  /// Now Repeat using the RRS
  aws_iobuf_reset ( bf );
  aws_set_rrs ( 1 ) ;
  rv = putObject ( "test.rrs", bf );
  printf ( "RV %d\n", rv );
  printf ( "CODE    [%d] \n", bf->code );
  printf ( "RESULT  [%s] \n", bf->result );
  printf ( "LEN     [%ld] \n", bf->len );
  printf ( "LASTMOD [%s] \n", bf->lastMod );
  printf ( "ETAG    [%s] \n", bf->eTag );

  return 0;
}
