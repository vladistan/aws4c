#ifndef  __AWS4C_EXTRA__
#define  __AWS4C_EXTRA__

#ifdef __cplusplus
extern "C" {
#endif


#include "aws4c.h"

#include <libxml/parser.h>      // from libxml2
#include <libxml/tree.h>

/* display a simple error message (i.e. errno is not set) and terminate execution */
#define AWS4C_ERR(MSG)                                                  \
   do {                                                                 \
       fprintf(stderr, "aws4c ERROR: %s, (%s:%d)\n",                    \
                  MSG, __FILE__, __LINE__);                             \
       fflush(stdout);                                                  \
       /* MPI_Abort(MPI_COMM_WORLD, -1);  */                            \
   } while (0)



CURLcode  s3_create_bucket(IOBuf* b, char* const bucket);
CURLcode  s3_stat_bucket  (IOBuf* b, char* const bucket);
CURLcode  s3_delete_bucket(IOBuf* b, char* const bucket);
CURLcode  s3_stat_object  (IOBuf* b, char* const bucket, char* const object);

void get_bucket_list(IOBuf* results);
void get_object_list(const char* bucket_name, IOBuf* results);


void debug_iobuf_node     (IOBufNode* n, int show_contents);
void debug_iobuf_node_list(IOBufNode* n, int show_contents);

void debug_iobuf(IOBuf* b, int nodes_too, int node_contents_too);

void        print_element_names(xmlNode* n);

xmlNode*    find_xml_element_of_type(xmlNode* n, xmlElementType type);
const char* find_element_of_type    (xmlNode* n, xmlElementType type);

xmlNode*    find_xml_element_named(xmlNode* n, const char* name);
const char* find_element_named    (xmlNode* n, const char* name);

void        parse_elements_named(xmlNode* n, char* name, IOBuf* results);
void        parse_XML_components(const char* field_name, IOBuf* results);




#ifdef __cplusplus
}
#endif

#endif
