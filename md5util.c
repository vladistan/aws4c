#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <openssl/md5.h>
#include "md5util.h"

#define   BUF_SIZE    4096

int
verifyMD5(char * filename, char * md5sum) {
  FILE  *       fd;
  int           i;
  int           nb_read;
  char          result[33];
  unsigned char hash[MD5_DIGEST_LENGTH];
  char          buff[BUF_SIZE];
  MD5_CTX       c;
 
  MD5_Init(&c);
  memset(hash, 0, MD5_DIGEST_LENGTH);
  memset(result, 0, 33);
  memset(buff, 0, BUF_SIZE);
  
  if( (fd = fopen( filename, "r" )) == NULL) {
      return -1;
  }
  
  while ((nb_read = fread( buff, sizeof(unsigned char), BUF_SIZE - 1, fd))) {
      MD5_Update(&c, buff, nb_read);
      memset(buff, 0, BUF_SIZE);
  }
  MD5_Final(hash, &c);
  for (i = 0; i < MD5_DIGEST_LENGTH; ++i) {
    sprintf(result + i * 2, "%02x", hash[i]);
  }
  
  if( (strcmp(md5sum, result)) == 0) {
    // All is well, md5sum matches
    return 0;
  }
  else {
    // Something bad happened don't claim that we have uploaded the file
    return -1;
  }
}
