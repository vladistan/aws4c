
VERSION=0.5
DNAME="aws4c-${VERSION}"

CFLAGS = -g -Wall
all: s3_get s3_put sqs_example s3_delete s3util

aws4c.o: aws4c.h

s3_get: aws4c.o 
s3_put: aws4c.o 
s3_delete: aws4c.o 
sqs_example: aws4c.o 
s3util: aws4c.o

dist:
	mkdir ${DNAME}
	cp `cat MANIFEST` ${DNAME}
	tar -czf aws4c.${VERSION}.tgz ${DNAME}

clean:
	$(RM) *.exe
	$(RM) s3_get s3_put s3_delete sqs_example aws4c.o
	$(RM) s3util
	$(RM) *.tgz
	$(RM) -r ${DNAME}
	

LDLIBS=`curl-config --libs` -lcrypto
