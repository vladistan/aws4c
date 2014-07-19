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

/// \file sqs_example.c
/// This is an example that shows how to use SQS functions of AWS4C.
/// The example performs the following actions
///
///    Initializes the library
///    Creates the queue
///    Obtains the full URL of the queue
///    Gets the queue parameters
///    Sets the visibility timeout of the queue
///    Puts a number of messages into the queue
///    Reads a number of messages from the queue 
///    Deletes the processed messages

#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include "aws4c.h"


/// This helper function that prints out a result of the operation
/// and data returned by the server
void DumpResult ( int rv, IOBuf * bf )
{
  printf ( "RV %d\n", rv );
  printf ( "CODE [%d] \n", bf->code );
  printf ( "RESULT  [%s] \n", bf->result );
  printf ( "LEN     [%d] \n", bf->len );
  if ( bf->lastMod) { printf ( "LASTMOD [%s] \n", bf->lastMod ); }
  if ( bf->eTag)    { printf ( "ETAG    [%s] \n", bf->eTag ); }

  while (-1)
    {
      char Ln[1024];
      int sz = aws_iobuf_getline ( bf, Ln, sizeof(Ln));
      if ( Ln[0] == 0 ) { break; }
      printf ( "S[%3d] %s", sz, Ln );
    }

}


int main ( int argc, char * argv[] )
{
  char * queueURL;

  /// Initialize the library.
  aws_init ();
  aws_set_debug ( 0 );
  int rc = aws_read_config  ( "ncred" );
  if ( rc )
    {
      puts ( "Could not find a credential in the config file" );
      puts ( "Make sure your ~/.awsAuth file is correct" );
      exit ( 1 );
    }
 
  /// Create the queue
  IOBuf * bf = aws_iobuf_new ();
  int rv = sqs_create_queue ( bf, "AWSCSQSSampleXX4" );
  if ( rv || bf->code != 200 )
    { 
      /// Note that failure to create the queue suggests either a genuine
      /// error or simply that the queue already exists.
      /// For most applications, it is okay to continue even if this
      /// operation fails.
      puts ( "Failed to create queue\n" );
      DumpResult ( rv, bf ); 
    }
  aws_iobuf_free(bf);

  /// Get the URL of the queue.
  /// Most applications require the full URL of the queue thath might be 
  /// different from the queue name passed to the SQSCreate queue
  bf = aws_iobuf_new ();
  rv = sqs_list_queues ( bf, "AWSCSQSSampleXX4" );
  if ( rv || bf->code != 200 )
    { 
      puts ( "Failed to retrieve list of queues.\n" );
      DumpResult ( rv, bf ); 
      exit(0); 
    }

  while (-1)
    {
      char Ln[1024];
      aws_iobuf_getline ( bf, Ln, sizeof(Ln));
      if ( Ln[0] == 0 ) { break; }
      Ln[strlen(Ln)-1] = 0;
      printf ( "Queue: [%s]\n",  Ln );
      queueURL = strdup(Ln);
    }

  if ( queueURL == NULL ) 
     { printf ( "Could not find our queue" ); exit(0); }

  bf = aws_iobuf_new ();
  int timeOut, qLength;
  rv = sqs_get_queueattributes ( bf, queueURL, &timeOut, &qLength );
  if ( !rv && bf->code == 200 )
    {
      printf ( "Queue Timeout = %d  Approximate Length  =  %d \n",  
	       timeOut, qLength );
    }
  else
    {
      DumpResult(rv, bf); exit(0);
    }
    
  bf = aws_iobuf_new ();
  rv = sqs_set_queuevisibilitytimeout ( bf, queueURL, 16 );
  if ( rv || bf->code != 200 )
    {
      DumpResult(rv, bf); exit(0);
    }

  /// Send a few messages
  int i;
  for ( i = 0 ; i < 18 ; i ++ )
    {
      puts ( "Send message" );
      bf = aws_iobuf_new ();
      char Message[256];
      snprintf(Message, sizeof(Message), "Msg #%d  \n\n L=%d\n <A>&lt;</ResponseMetaData>", i, i);
      rv = sqs_send_message ( bf, queueURL, Message );
      if ( rv || bf->code != 200 ) { DumpResult(rv, bf); exit(0); }
    }
  /// Retrieve messages.
  for ( i = 0 ; i < 500 ; i ++ )
  {
      puts ( "Get Message" );
      bf = aws_iobuf_new ();
      char receipt[1024];
      memset ( receipt, 0, sizeof(receipt));
      rv = sqs_get_message ( bf, queueURL, receipt );
      DumpResult(rv, bf); 
      puts ("\n----");
      printf ( "ID: %s\n", receipt );
      aws_iobuf_free(bf);

      if ( receipt[0] != 0 )
	{
	  bf = aws_iobuf_new ();
	  rv = sqs_delete_message ( bf, queueURL, receipt );
	  if ( rv || bf->code != 200 ) { DumpResult(rv, bf); }
	}    
      else { puts ( "Empty queue" ); break; }
  }
  puts ("\n---");
  return 0;
}
