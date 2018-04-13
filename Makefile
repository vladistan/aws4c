all: s3_get s3_put sqs_example s3_delete lib lib_extra test_aws

VERSION=0.5.4
DNAME="aws4c-${VERSION}"

LIB       = libaws4c
LIB_EXTRA = libaws4c_extra

# library itself is already findable in /usr/lib64
XML_INC_DIR = /usr/include/libxml2



ifdef DEBUG
	CFLAGS = -g
else
	CFLAGS = -g -O3
endif
CFLAGS += -Wall -I $(XML_INC_DIR) -L .


# -laws4c -laws4c_extra -lcurl -lxml2 -lcrypto
LDLIBS = `curl-config --libs` -lcrypto


# NOTE: There is now some code in test_aws.c which uses pthreads.
#       Because some installations won't support this, this
#       part of the code is not compiled by default.  To enable it,
#       must do something like this:
#
#          make [...] PTHREADS=1
#
#       where "[...]" can be empty, if you want to make everything.  You
#       can always 'make clean' then rebuild (with or without
#       "PTHREADS=1"), if you change your mind.

TEST_LIBS = -laws4c -laws4c_extra `curl-config --libs` -lxml2 -lcrypto

PTHREADS :=
ifneq ($(PTHREADS),)
   CFLAGS  += -DTEST_AWS_PTHREADS
   TEST_LIBS += -lpthread
endif




s3_get:      aws4c.o 
s3_put:      aws4c.o 
s3_delete:   aws4c.o 
sqs_example: aws4c.o 

lib: aws4c.o aws4c.h
	ar -cvr $(LIB).a $<

lib_extra: aws4c_extra.o aws4c_extra.h
	ar -cvr $(LIB_EXTRA).a $<


test_%: test_%.o lib lib_extra
	$(CC)  $(CFLAGS) $(LDFLAGS) -o $@ $< $(TEST_LIBS)

test:
	@ echo "THREADS = '$(THREADS)'"
	@ echo "CPPFLAGS = $(CPPFLAGS)"
	@ echo "TEST_LIBS = $(TEST_LIBS)"

%.o: %.c aws4c.h
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


