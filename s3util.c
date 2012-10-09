/*
* 
* Copyright(c) 2012,  Michael Cheah
*
*/ 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aws4c.h"
#include "md5util.h"

#define BUF_SIZE 128
#define LINE_MAX 4096

int   put_file( IOBuf * aws_buf, char *name );
int   get_file( IOBuf * aws_buf, char *name );
int   delete_file( IOBuf * aws_buf, char *name );
void  global_free();

char  *operation    = NULL;
char  *filename     = NULL;
char  *bucketname   = NULL;
char  *S3_host      = NULL;
// Valid values are {private | public-read | public-read-write | authenticated-read}
char  * S3_acl      = "private";

/*
 * Upload the specified file to the S3 storage
 * according to the configuration details
 */

int   put_file( IOBuf * aws_buf, char *name ) {
  FILE  * fp;
  char readbuf[BUF_SIZE];
  
  // Read in file to upload
  if( (fp = fopen(name, "rb")) == NULL) {
      fprintf(stdout, "ERROR: The specified file doesn't exist. \n");
      return -1;
    }
  else {
    int n;
    while( !feof(fp) ) {
      n = fread(readbuf, sizeof(unsigned char), BUF_SIZE, fp);
      if(n != BUF_SIZE) {
        if(feof(fp)) {
          ; // Do Nothing
        }
        else {
          fprintf(stdout, "ERROR: Error reading from file. \n");
          return -1;
        }
      }
      aws_iobuf_append ( aws_buf, readbuf, n);
    }
    fclose(fp);
  }
  
  int rv = s3_put(aws_buf, name);  
  return rv;
}


int   get_file( IOBuf * aws_buf, char *name ) {
  int rv = s3_get(aws_buf, name);
  if(rv == 0 && aws_buf->code == 200 && aws_buf->len != 0) {
    FILE  * fp;
    char writebuf[BUF_SIZE];
    
    // Write out the downloaded file
    // Check if file exists
    if( (fp = fopen(name, "rb")) != NULL) {
      fprintf(stdout, "WARNING: The specified file already exist. \n"
                      "Refuse to overwrite. \n");
      fclose(fp);
      return -1;
    }
    // File doesn't exist yet. Okay to write :)
    else {
      if( (fp = fopen(name, "w+")) == NULL) {
        fprintf(stdout, "ERROR: Unable to create the specified file. \n");
        return -1;
      }
      int n, sz;
      while( ( sz = aws_iobuf_getdata ( aws_buf, writebuf, sizeof(writebuf))) != 0 ) {
        n = fwrite(writebuf, sizeof(unsigned char), sz, fp);
        if(n != sz) {
            fprintf(stdout, "ERROR: Error writing to file. \n");
            return -1;
          }
      }
      fclose(fp);
    }
  }
  return rv;
}

int   delete_file( IOBuf * aws_buf, char *name ) {
  int rv = s3_delete(aws_buf, name);  
  return rv;
}





int
main (int argc, char *argv[]) {
  aws_init();
  if(argv[3] != NULL) {
    aws_set_debug(atoi(argv[3]));
  }
  IOBuf * aws_buf = aws_iobuf_new();
  
  // Read credential file
  int rv = aws_read_config("myteksi");
  if ( rv )
  {
    fprintf(stderr, "Could not find a credential in the config file \n" );
    fprintf(stderr, "Make sure your ~/.awsAuth file is correct \n" );
    exit (1);
  }
  
  
  // Read config file
  FILE *fp = NULL;
  
  char getline[ LINE_MAX * sizeof(char) ];
  if( (fp = fopen("s3config", "r")) == NULL) {
    //File does not exist. Initialize it
    if( (fp = fopen("s3config", "w+")) == NULL) {
      fprintf(stderr, "ERROR: Unable to create config file.\n");
      exit(0);
    }
    
    // Ask for bucket_name
    fprintf(stdout, "Config file doesn't exist yet! Creating one now. \n");
    fprintf(stdout, "Please specify the AWS S3 base address "
                    "[default s3.amazonaws.com] :");
    char getInput[ LINE_MAX * sizeof(char) ];
    if( fgets( getInput, sizeof(getInput) , stdin ) != NULL ) {
      if( strcmp(getInput, "\n") != 0 ) {
        S3_host = strndup(getInput, strlen(getInput) -1); // Remove trailing NL
      }
      else {
        S3_host = strdup("s3.amazonaws.com");
      }
    }
    
    int validbucketname = 0;
    while( !validbucketname ) {
      fprintf(stdout, "Please specify the bucket name: ");
      if( fgets( getInput, sizeof(getInput) , stdin ) != NULL ) {
        bucketname = strndup(getInput, strlen(getInput) -1);
        validbucketname = 1;
      }
    }
    
    char * buf = malloc( snprintf(NULL, 0, "S3_Base_Address=\"%s\"\n"
                                  "bucket_name=\"%s\"\n", S3_host, bucketname));
    sprintf(buf, "S3_Base_Address=\"%s\"\n"
                 "bucket_name=\"%s\"\n", S3_host, bucketname );
    
    if( fputs( buf, fp ) == EOF ) {
      fprintf(stderr, "ERROR: Unable to create config file.\n");
    }
  }
  // Config file exist, parse it
  else {
    char    delim[4] = {'=', '\"', '\n', '\0'};
    char*   left;
    char*   right;
    
    while( fgets( getline, sizeof(getline) , fp ) != NULL ) {
      if( (left = strtok(getline, delim)) != NULL ) {
        right = strtok(NULL, delim);
      }
      else {
        //Empty Line
      }
      
      // Match the strings
      char* comparison = "S3_Base_Address";
      if( strcmp(left, comparison) == 0) {
        if(right != NULL) {
          S3_host = strdup(right);
        }
        else {
          S3_host = strdup("s3.amazonaws.com");
        }
      }
      
      comparison = "bucket_name";
      if( strcmp(left, comparison) == 0 && right != NULL) {
          bucketname = strdup(right);
      }
    }  // End while
    
    if( S3_host == NULL || bucketname == NULL ) {
      fprintf(stderr, "ERROR: Invalid entry in config file.\n");
    }
  }
  
  // Set parameters in S3 library
  s3_set_host(S3_host);
  s3_set_bucket(bucketname);
  s3_set_acl(S3_acl);
  
  // Check for valid arguments
  if ( argc != 3 && argc != 4 ) {
    fprintf(stderr, "Usage: s3util <operation> <filename>\n");
    fprintf(stderr, "Operation can be one of {PUT, GET, DELETE}\n");
    exit(1);
  }
  // Check if operation is valid
  operation = strdup(argv[1]);
  filename  = strdup(argv[2]);
  
  // PUT file
  if( strcmp(operation, "PUT") == 0 ) {
    int rc;
    char s3replyMD5[33];
    
    rv = put_file( aws_buf, filename );
    if( aws_buf->eTag != NULL && strlen(aws_buf->eTag) > 2 ) {
      memset(s3replyMD5, 0, 33);
      memcpy(s3replyMD5, aws_buf->eTag + 1, 32);
      rc = verifyMD5(filename, s3replyMD5);
    }
    if(rc != 0) {
      return rc;
    }
    printf ( "MD5SUM matches, file uploaded successfully \n" );
  }
  
  // GET file
  else if( strcmp(operation, "GET") == 0 ) {
    rv = get_file( aws_buf, filename );
  }
  
  // DELETE FILE
  else if( strcmp(operation, "DELETE") == 0 ) {
    rv = delete_file( aws_buf, filename );
  }
  else {
    fprintf(stderr, "Invalid operation, operation must be one of "
    "{PUT, GET, DELETE}\n");
    exit(1);
  }
  
  printf ( "RV %d\n", rv );
  printf ( "CODE    [%d] \n", aws_buf->code );
  printf ( "RESULT  [%s] \n", aws_buf->result );
  printf ( "LEN     [%d] \n", aws_buf->len );
  printf ( "LASTMOD [%s] \n", aws_buf->lastMod );
  printf ( "ETAG    [%s] \n", aws_buf->eTag );
  aws_iobuf_free(aws_buf);
  
  global_free();
  return 0;
}

// Free up memory allocated. Call at the end of the program
void
global_free() {
  if( operation != NULL )    free(operation);
  if( filename != NULL )     free(filename);
  if( bucketname != NULL )   free(bucketname);
  if( S3_host != NULL )      free(S3_host);
  aws_deinit();
  return;
}
