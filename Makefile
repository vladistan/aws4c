
VERSION=0.5
DNAME="aws4c-${VERSION}"
LDLIBS+=`curl-config --libs` -lcrypto

CFLAGS = -g -Wall 
all: s3_get s3_put sqs_example s3_delete

ifeq ($(WITH_COVERAGE),Y)
CFLAGS += -g -pg -fprofile-arcs -ftest-coverage -Wno-write-strings -DTESTING=1
endif


ifeq ($(TEST),Y)
CFLAGS += -Dfprintf=mock_fprintf -Dvfprintf=mock_fvprintf -Dtime=mock_time -DTESTING=1 -I.
CXXFLAGS += $(CFLAGS)
endif

CXXFLAGS = $(CFLAGS)

aws4c.o: aws4c.h

s3_get: aws4c.o
s3_put: aws4c.o 
s3_delete: aws4c.o 
sqs_example: aws4c.o 

dist:
	mkdir ${DNAME}
	cp `cat MANIFEST` ${DNAME}
	tar -czf aws4c.${VERSION}.tgz ${DNAME}

clean:
	-rm *.exe
	-rm s3_get s3_put sqs_example
	-rm *.tgz
	-rm -rf ${DNAME}
	-rm aws4_test.o  aws4c.o
	
test: aws4c.o mocks.o aws4c_test.o
	$(CXX) $(CXXFLAGS) -o $@ $^  $(LDLIBS)  -lCppUTest -lCppUTestExt --coverage

