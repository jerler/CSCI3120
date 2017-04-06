#ifndef CACHE_LINK_LIST_H_
#define CACHE_LINK_LIST_H_

/* This is a singly linked list implementation as used by cache.c.  It only
 * supports push_front.  It does not take ownership of the void* items that
 * are passed it, that memory is up to the caller to manage. */
struct link_list;

typedef void(*link_list_dtor)( void* );

/* Create a linked list object.  If the dtor function pointer is non-null
 * then it will be called on each item when removing it from the list.
 * This can be a user supplied custom function, a pointer to the free()
 * function, or null (for no dtor semantics). */
struct link_list* link_list_init( link_list_dtor );
void link_list_destroy( struct link_list* );

/* Is the list empty?  1 for yes, 0 for no */
int link_list_empty( const struct link_list* );

typedef unsigned int(*link_list_find_predicate)( void* context, void* item );
/* Calls the user supplied find_predicate function on each item in the list
 * and returns a pointer to the first one that find_predicate returns
 * non-zero.  Otherwise, returns a null pointer. */
void* link_list_find( const struct link_list*,link_list_find_predicate, void* context );

typedef void(*link_list_visit_predicate)( void* context, void* item );
/* Calls visit_predicate, passing it a pointer to the context variable,
 * for each item in the list. */
void link_list_foreach( struct link_list*,link_list_visit_predicate, void* context );

/* Add an item to the front of the list.  Returns 1 if successful, 0 if not. */
int link_list_add_front( struct link_list*, void* );

/* Remove the item from the list.  no-op if it was not in the list. */
void link_list_remove( struct link_list*, void* );

#endif /* CACHE_LINK_LIST_H_ */

