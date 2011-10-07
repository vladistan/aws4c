
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
#include <string.h>
#include "aws4c.h"


int putObject ( char * name, IOBuf * bf )
{
  int i;
  for ( i = 0 ; i < 9000 ; i ++ )
    {
      char S[128];
      snprintf ( S,sizeof(S), "Ln %d \n" , i );
      aws_iobuf_append ( bf,S, strlen(S));
    }
  return s3_put ( bf, name );
}

int main ( int argc, char * argv[] )
{
  int rv;

  aws_init ();
  aws_set_debug  ( 0 );
  int rc = aws_read_config  ( "sample" );
  if ( rc )
    {
      puts ( "Could not find a credential in the config file" );
      puts ( "Make sure your ~/.awsAuth file is correct" );
      exit ( 1 );
    }

  s3_set_bucket ("aws4c.samples");
  s3_set_mime ("text/plain");
  s3_set_acl ("public-read");

  IOBuf * bf = aws_iobuf_new ();

  rv = putObject ( "aws4c.samplefile", bf );
  printf ( "RV %d\n", rv );

  printf ( "CODE    [%d] \n", bf->code );
  printf ( "RESULT  [%s] \n", bf->result );
  printf ( "LEN     [%d] \n", bf->len );
  printf ( "LASTMOD [%s] \n", bf->lastMod );
  printf ( "ETAG    [%s] \n", bf->eTag );

  while(-1)
    {
  char Ln[1024];
  int sz = aws_iobuf_getline ( bf, Ln, sizeof(Ln));
  if ( Ln[0] == 0 ) break;
    printf ( "S[%3d] %s", sz, Ln );
    }

  /// Now Repeat using the RRS
  bf = aws_iobuf_new ();
  aws_set_rrs ( 1 ) ;
  rv = putObject ( "aws4c.samplefile.rrs", bf );
  printf ( "RV %d\n", rv );
  printf ( "CODE    [%d] \n", bf->code );
  printf ( "RESULT  [%s] \n", bf->result );
  printf ( "LEN     [%d] \n", bf->len );
  printf ( "LASTMOD [%s] \n", bf->lastMod );
  printf ( "ETAG    [%s] \n", bf->eTag );



  return 0;
}
