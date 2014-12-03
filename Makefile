all: s3_get s3_put sqs_example s3_delete lib lib_extra test_aws

VERSION=0.5.2
DNAME="aws4c-${VERSION}"

LIB       = libaws4c
LIB_EXTRA = libaws4c_extra

# library itself is already findable in /usr/lib64
XML_INC_DIR = /usr/include/libxml2



CFLAGS = -g -Wall -I $(XML_INC_DIR) -L .

LDLIBS=`curl-config --libs` -lcrypto



aws4c.o: aws4c.h

s3_get:      aws4c.o 
s3_put:      aws4c.o 
s3_delete:   aws4c.o 
sqs_example: aws4c.o 

lib: aws4c.o aws4c.h
	ar -cvr $(LIB).a $<

lib_extra: aws4c_extra.o aws4c_extra.h
	ar -cvr $(LIB_EXTRA).a $<

test_%: test_%.o lib lib_extra
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -laws4c -laws4c_extra -lcurl -lxml2 -lcrypto

%.o: %.c
	$(CC) -c $(CFLAGS) $(LDFLAGS) -o $@ $<


dist:
	mkdir ${DNAME}
	cp -rp `cat MANIFEST` ${DNAME}
	tar -czf aws4c-${VERSION}.tgz ${DNAME}

clean:
	-rm -f *~
	-rm -f *.exe
	-rm -f *.o
	-rm -f s3_get s3_put s3_delete sqs_example test_aws
	-rm -f *.tgz
	-rm -rf ${DNAME}
	-rm -f $(LIB).*
	-rm -f $(LIB_EXTRA).*


