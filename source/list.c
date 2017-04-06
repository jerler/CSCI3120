#include <stdlib.h>

#include "list.h"

struct node {
  struct node* next;
  void* data;
};

struct link_list {
  struct node* head;
  link_list_dtor dtor;
};

struct link_list* link_list_init( link_list_dtor dtor ) {
  struct link_list* l = calloc( sizeof( struct link_list ), 1 );
  if( l ) l->dtor = dtor;
  return l;
}

void link_list_destroy( struct link_list* l )
{
  struct node* pos = l->head;
  while( pos )
  {
    struct node* tmp = pos;
    pos = pos->next;
    if( l->dtor ) l->dtor( tmp->data );
    free( tmp );
  }
  free( l );
}

int link_list_empty( const struct link_list* l )
{
  return NULL == l->head;
}

void* link_list_find( const struct link_list* l,link_list_find_predicate f, void* context )
{
  struct node* pos = l->head;
  while( pos )
  {
    if( f( context, pos->data )) return pos->data;
    pos = pos->next;
  }
  return NULL;
}

void link_list_foreach( struct link_list* l,link_list_visit_predicate f, void* context )
{
  struct node* pos = l->head;
  while( pos )
  {
    f( context, pos->data );
    pos = pos->next;
  }
}

int link_list_add_front( struct link_list* l, void* item )
{
  struct node* node = calloc( sizeof( struct node ), 1 );
  if( !node ) return 0;
  node->next = l->head;
  node->data = item;
  l->head = node;
  return 1;
}

void link_list_remove( struct link_list* l, void* item )
{

  struct node* pos = l->head;
  struct node* prev = NULL;
  
  while( pos && pos->data != item ) 
  {
    prev = pos;
    pos = pos->next;
  }
  /* May not have found a match, or the list is empty */
  if( !pos ) return;

  if( !prev )
  {
    /* It's the head */
    l->head = pos->next;
  }
  else 
  {
    /* It's not the head */
    prev->next = pos->next;
  }

  if( l->dtor ) l->dtor( pos->data );
  free( pos );

}

