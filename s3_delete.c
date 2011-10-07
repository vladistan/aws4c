

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
#include "aws4c.h"




int main ( int argc, char * argv[] )
{

  

  aws_init ();
  aws_set_debug ( 1 );
  int rc = aws_read_config  ( "sample" );
  if ( rc )
    {
      puts ( "Could not find a credential in the config file" );
      puts ( "Make sure your ~/.awsAuth file is correct" );
      exit ( 1 );
    }

  s3_set_host ( "s3.amazonaws.com");
  s3_set_bucket ("aws4c.samples");


  IOBuf * bf = aws_iobuf_new ();
  int rv = s3_delete ( bf, "aws4c.samplefile" );

  printf ( "RV %d\n", rv );

  printf ( "CODE    [%d] \n", bf->code );
  printf ( "RESULT  [%s] \n", bf->result );
  printf ( "LEN     [%d] \n", bf->len );
  printf ( "C-LEN   [%d] \n", bf->contentLen );
  printf ( "LASTMOD [%s] \n", bf->lastMod );
  printf ( "ETAG    [%s] \n", bf->eTag );

  aws_iobuf_free ( bf );

  return 0;
}
