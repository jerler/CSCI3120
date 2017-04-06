#include "cache.h"
#include <assert.h>
#include <stdio.h>

int main()
{

  const int size = 20;
  cache_init( size );
  {
    int bad = cache_open( "there is no way this file exists" );
    assert( bad == -1 );
  }
  int cfd_id = cache_open( "testfile" );
  assert( -1 != cfd_id );

  FILE* out = fopen( "output", "wb" );
  assert( out );

  /* Read contents of our cached implementation into the file descriptor */
  assert( 1 == cache_send( cfd_id, fileno( out ), 1 ));
  assert( 10 == cache_send( cfd_id, fileno( out ), 10 ));
  assert( 0 == cache_send( cfd_id, fileno( out ), 10 ));

  /* This should end up in the non-cached list */
  int cfd_id2 = cache_open( "testfile2" );
  assert( -1 != cfd_id2 );

  /* Read contents of our non-cached implementation into the file descriptor */
  assert( 1 == cache_send( cfd_id2, fileno( out ), 1 ));
  assert( 10 == cache_send( cfd_id2, fileno( out ), 10 ));
  assert( 0 == cache_send( cfd_id2, fileno( out ), 10 ));

  /* Should have a cache-hit for this */
  int cfd_id3 = cache_open( "testfile" );
  assert( -1 != cfd_id3 );

  assert( -1 != cache_close( cfd_id ));
  assert( -1 != cache_close( cfd_id2 ));
  assert( -1 != cache_close( cfd_id3 ));

  /* Re-open testfile2, and it should replace the testfile in the cache */
  cfd_id = cache_open( "testfile2" );
  assert( -1 != cfd_id );
  assert( -1 != cache_close( cfd_id ));

  /* Re-open testfile and it will now end up in the non-cached list */
  cfd_id2 = cache_open( "testfile" );
  assert( -1 != cfd_id );
  assert( -1 != cache_close( cfd_id ));

  fclose( out );

  cache_destroy();

  return 0;
}
