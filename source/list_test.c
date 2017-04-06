#include "list.h"
#include <stdlib.h>
#include <assert.h>

unsigned int find( void* context, void* item ) {
  int* a = context;
  int* b = item;
  if( *a == *b ) return 1;
  return 0;
}

void visit_counter( void* context, void* item ) {
  unsigned int* count = context;
  (*count)++;
}

unsigned int finder( void* context, void* item ) {
  int* a = context;
  int* b = item;
  return *a == *b;
}

void my_dtor( void* item ) {

  /* Do some custom stuff.... */

  /* free( item ) */
}

int main() {

  unsigned int count = 0;
  unsigned int count2 = 0;
  int find_value = 0;
  int data[] = { 1,2,3,4,5,6 };
  int* ptr = &data[0];
  unsigned int i;
  struct link_list* l = link_list_init( my_dtor );

  assert( link_list_empty( l ));

  for( i = 0; i < sizeof( data ) / sizeof( int ); ++i ) {
    link_list_add_front( l, ptr++ );
  }
  link_list_foreach( l, visit_counter, &count );
  assert( 6 == count );
  assert( ! link_list_empty( l ));

  find_value = 1;
  assert( NULL != link_list_find( l, finder, &find_value ));

  find_value = 23;
  assert( NULL == link_list_find( l, finder, &find_value ));

  ptr = &data[0];
  for( i = 0; i < 3; ++i ) {
    link_list_remove( l, ptr++ );
  }
  link_list_foreach( l, visit_counter, &count2 );
  assert( 3 == ( count - count2 ));

  link_list_destroy( l );

  return EXIT_SUCCESS;

}

