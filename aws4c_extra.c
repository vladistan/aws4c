#include <assert.h>

#include <string.h>             /* strcmp() */
#include <stdlib.h>             /* exit() */
#include <unistd.h>             /* sleep() */
#include <stdio.h>

#include "aws4c.h"
#include "aws4c_extra.h"

#define  BUFF_LEN  1024
char     buff[BUFF_LEN];




// ---------------------------------------------------------------------------
// higher-level functions
// ---------------------------------------------------------------------------

// Create a bucket
CURLcode s3_create_bucket ( IOBuf * b, char * const bucket )
{
  s3_set_bucket(bucket);
  CURLcode sc = s3_put(b, "");
  return sc;
}

// Delete a bucket
CURLcode s3_delete_bucket ( IOBuf * b, char * const bucket )
{
  s3_set_bucket(bucket);
  CURLcode sc = s3_delete(b, "");
  return sc;
}

// Stat a bucket
CURLcode s3_stat_bucket ( IOBuf * b, char * const bucket )
{
  s3_set_bucket(bucket);
  CURLcode sc = s3_head(b, "");
  return sc;
}

// Stat an object
CURLcode s3_stat_object ( IOBuf * b, char * const bucket, char * const object )
{
  s3_set_bucket(bucket);
  CURLcode sc = s3_head(b, object);
  return sc;
}

// After this, successive calls to "aws_get_line(results)" will return
// successive bucket-names in the top-level (accessible by the user).
void
get_bucket_list(IOBuf* results) {
   s3_set_bucket(NULL);
   parse_XML_components("Name", results);
}

// After this, successive calls to "aws_get_line(results)" will return
// successive object-names in the bucket.
void
get_object_list(const char* bucket_name, IOBuf* results) {
   // TBD: save the curent bucket, and restore at exit.
   s3_set_bucket((char*)bucket_name);
   parse_XML_components("Key", results);
}



// ---------------------------------------------------------------------------
// diagnostics
// ---------------------------------------------------------------------------

// show diagnostics for IOBufNode
void
debug_iobuf_node(IOBufNode* n, int show_node_contents) {
   if (!n)
      printf("IOBufNode  NULL\n");
   else {
      printf("IOBufNode  0x%lx\n", (size_t)n);

      if (show_node_contents) {
         int i;
         printf("  buf      0x%lx --> '", (size_t)n->buf);
         for (i=0; i<n->write_count; ++i)
            printf("%c", n->buf[i]);
         printf("'\n");
      }
      else
         printf("  buf      0x%lx", (size_t)n->buf);

      printf("  len      %ld\n", n->len);
      printf("  wrt_ct   %ld\n", n->write_count);
      printf("  static?  %d\n",  n->is_static);
      printf("  next     0x%lx\n", (size_t)n->next);
   }
   printf("\n");

}

void
debug_iobuf_node_list(IOBufNode* n, int show_contents) {
   IOBufNode* ptr;
   for (ptr=n; n; n=n->next) {
      debug_iobuf_node(n, show_contents);
   }
}

// show diagnostics for IOBuf
void
debug_iobuf(IOBuf* b, int nodes_too, int node_contents_too) {
   printf("IOBuf         0x%lx\n", (size_t)b);
   printf("  len         %ld\n",   b->len);
   printf("  write_count %ld\n",   b->write_count);
   printf("\n");

   printf("  first       0x%lx\n", (size_t)b->first);
   printf("  last        0x%lx\n", (size_t)b->last);
   printf("\n");

   printf("  read        0x%lx\n", (size_t)b->reading);
   printf("  read_pos    0x%lx\n", (size_t)b->read_pos);
   printf("\n");

   printf("  write       0x%lx\n", (size_t)b->writing);
   printf("\n");

   printf("  lastMod     '%s'\n",  b->lastMod);
   printf("  eTag        '%s'\n",  b->eTag);
   printf("  contentLen   %lu\n",  b->contentLen);
   printf("  code         %d\n",   b->code);
   printf("  result      '%s'\n",  b->result);
   printf("\n");

   if (nodes_too)
      debug_iobuf_node_list(b->first, node_contents_too);
}




// ...........................................................................
// These are helpers for traversing the trees created by libxml2, when
// it parses raw XML text.
//
// parsed XML structure has a root that looks something like this:
// 
//     xmlNode = { name=<string>,
//                 type=XML_ELEMENT_NODE,
//                 next=(xmlNode*) ...,
//                 children=(xmlNode*) ...,
//     }
//
// The "children" may be another layer of XML_ELEMENT_TYPE, or they may
// be type=XML_TEXT_NODE, etc.
// ...........................................................................

void
print_element_names(xmlNode* n) {

   xmlNode* cur_node = NULL;

   for (cur_node = n; cur_node; cur_node = cur_node->next) {
      if (cur_node->type == XML_ELEMENT_NODE) {
         printf("node type: Element, name: %s\n", cur_node->name);
      }
      print_element_names(cur_node->children);
   }
}









// find the first XML element (depth-first) with type <type>
xmlNode*
find_xml_element_of_type(xmlNode* n, xmlElementType type) {

   xmlNode* cur_node = NULL;
   for (cur_node=n; cur_node; cur_node=cur_node->next) {
      if (cur_node->type == type) {
         // printf("found type: %d -> content: '%s'\n",
         //        cur_node->type,
         //       cur_node->content);
         return cur_node;
      }
      xmlNode* sub = find_xml_element_of_type(cur_node->children, type);
      if (sub)
         return sub;
   }

   return NULL;
}

// like find_xml_element_type, but return the text content of the element
const char*
find_element_of_type(xmlNode* n, xmlElementType type) {

   xmlNode* cur_node = find_xml_element_of_type(n, type);
   return ((cur_node) ? (char*)cur_node->content : NULL);
}












// find the first XML element (depth_first) with tag matching <name>
xmlNode*
find_xml_element_named(xmlNode* n, const char* name) {

   // traverse nodes at a given nesting level, looking for named tag.
   xmlNode* cur_node = NULL;
   for (cur_node = n; cur_node; cur_node = cur_node->next) {

      if (cur_node->type == XML_ELEMENT_NODE) {
         if (! strcmp(name, (const char*)cur_node->name)) {
            // printf("found name: '%s'\n", name);
            return cur_node;
         }
      }

      // maybe find it recursively, at deeper nesting levels?
      xmlNode* sub = find_xml_element_named(cur_node->children, name);
      if (sub)
         return sub;
   }
   return NULL;
}

// Call find_xml_element_named(), then find the first TEXT_NODE
// child of *that* node, and return the text-content of that child.
const char*
find_element_named(xmlNode* n, const char* name) {

   // traverse nodes at a given nesting level, looking for named tag.
   xmlNode* tag = find_xml_element_named(n, name);
   if (tag) {
      // find the text-value contents for the located tag
      return find_element_of_type(tag->children, XML_TEXT_NODE);
   }
   return NULL;
}










// === custom for test_aws.c

// Collect the text-component of XML tags matching <name>.  Instead of
// printing all of them, or printing the first one, etc, we append them
// into the <results> IOBuf.  You can access them there by calling
// aws_iobuf_getline().  Successive calls will return successive results.
// (See test_listing(), below.)
void
parse_elements_named(xmlNode* n, char* name, IOBuf* results) {

   // traverse nodes at a given nesting level, looking for named tag.
   xmlNode* cur_node = NULL;
   for (cur_node = n; cur_node; cur_node = cur_node->next) {

      if (cur_node->type == XML_ELEMENT_NODE) {
         if (! strcmp(name, (const char*)cur_node->name)) {
            // printf("found name: '%s'\n", name);

            // find the text-value contents for the located tag
            const char* sub = find_element_of_type(cur_node->children, XML_TEXT_NODE);
            if (sub) {
               if (results) {
                  aws_iobuf_append(results, (char*)sub, strlen(sub));
                  aws_iobuf_append(results, "\n", 1); /* for aws_iobuf_getline() */
               }
               else
                  printf("--> '%s'\n", sub);
            }
         }
      }

      // maybe find name recursively, at deeper nesting levels?
      parse_elements_named(cur_node->children, name, results);
   }
}


// NOTE: Parse XML from the response that was received from a request.  We
//       assume the IOBuf was set up prior to the request, such that the
//       entire response would be saved contiguously in a single IOBufNode
//       at the front of the list.  This can be done something like this:
//
//           IOBuf* b;
//           aws_iobuf_reset(b);
//           aws_iobuf_extend_...(b, my_buf);
//           // some request ...  (e.g. s3_get())
//
//       NOTE: don't forget to call aws_iobuf_reset() afterwards, so that
//       the iobuf will relinquish its pointer to your buffer.
//
// Use field-name "Name" when listing buckets
// Use field-name "Key" when listing objcets in a bucket
//
void
parse_XML_components(const char* field_name, IOBuf* results) {

   // create private query-IOBuf, using <buff> as storage.
   //
   // NOTE: the XML parser doesn't know anything aobut aws_iobuf_getline().
   //       Therefore, the response must fit entirely into one IOBufNode.
   //       We must allocate something big enough here (or call
   //       aws_iobuf_realloc(), below).
   IOBuf* b = aws_iobuf_new();
   aws_iobuf_reset(b);
   aws_iobuf_extend_static(b, buff, BUFF_LEN);
   aws_iobuf_growth_size(b, BUFF_LEN);

   // response will include XML listing of buckets or objects
   AWS4C_CHECK( s3_get(b, "") );

   aws_iobuf_realloc(b);        /* in case we overflowed <buff> */

   // prepare for XML parse
   xmlDocPtr doc = xmlReadMemory(b->first->buf,
                                 b->first->len,
                                 NULL, NULL, 0);
   if (doc == NULL) {
      AWS4C_ERR("Failed to find POST response\n");
   }

   // navigate parsed XML-tree to find text-components for desired field
   xmlNode* root_element = xmlDocGetRootElement(doc);
   parse_elements_named(root_element, (char*)field_name, results);

   // drop IOBufNode pointers to <buff>
   aws_iobuf_free(b);
}

