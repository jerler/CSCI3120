/*
 * cache.c
 *
 *  Created on: Mar 21, 2017
 *      Author: Julie Morrissey - B00592466
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h> //for inode
#include <sys/sendfile.h> //SYSTEM DEPENDENT - USE LINUX

/*
 * Basic design: We have vectors (assigned using calloc) of size 1 to start for clients (struct cfd_mgr has vector of struct cfds), files that were opened but
 * not cached (struct files_not_cached_mgr has vector of struct files_not_cached), and later (to be implmeneted) for files that were copied into the cache
 * 
 * Whenever we run out of space in any of those vectors, we reallocate the stored information into a new vector that has doubled in size. In the case of the file
 * vectors, we need to copy their pointer information before reallocating so we can set the pointers from the cfds back up after reallocation. For cfd and files not
 * in the cache, this process will continue unbounded. For the cache, there needs to be a max cut-off point (i.e. a full cache) at which point the file either needs 
 * to take the place of an unused, old file in the cache, or be opened from disk (the part I have already implemented). No vectors will ever shrink.
 * 
 * There are versions of cache_send, cache_filesize, and cache_close that are specific to whether the file has been saved in the cache or
 * if it has been opened from disk. The user does not need to worry about that however and only of course needs to call one of the above mentioned methods and this 
 * deciding is all handled within this program (using the three pointers in our typedef). Similarly, whoever writes the cache part of the code need only to copy the 
 * methods I've made (ex: filesize_v_not_cached (v for version)) for the cache-specific methods since all the deciding about what type of file it is has already been
 * written and successfully compiled in my cache_open method. (though make sure you notice what states are updated in these lower level methods and mirror them in yours!)
 * 
 * For the most part I haven't delved into the cache-specific architecture but I have created cache_pages which is a struct that's used for every file copied into the cache
 * and contains information such as how many people (cfds) are currently using the file, a pointer to where the data has been copied, etc., as well as a files_cached struct
 * to mirror my files_not_cached struct, which contains information just about where the cfd user is currently pointing to in the cached memory (so one of these structs is
 * made per cfd and is used as an intermediary between the cfd and the cache)
 * 
 * I'm not sure if all of the empty structs down below will end up being used but I've temporarily left their skeletons if for no other reason than for you to orient
 * yourself with my logic. :-) 
 */




struct cfd;

//Define function pointer that will be used to point to the function specific to whether the file is cached or not
typedef int (*cache_filesize_fptr) (struct cfd*);
typedef int (*cache_send_fptr) (struct cfd*,int client_fd, int n_bytes);
typedef int (*cache_close_fptr) (struct cfd*);



/* Structs */

// Every client gets a cfd
struct cfd {
	int id;           //The ID of the client
	void* interface;  //points to either a file_cached or a file_uncached
	int taken;        // boolean; if this space is taken by a client
	//function pointers
	cache_filesize_fptr filesize_ptr;
	cache_send_fptr send_ptr;
	cache_close_fptr close_ptr;
};


// Holds the vector of cfds
struct client_mgr {
  int client_size;     // How many clients we currently have
  struct cfd* clients; // The vector of clients
};


// One of these for every entry in the cache - not yet initialized!
struct cache_page {
	ino_t inode;          // The unique identifier of the file (My understanding that inodes identify files even if not in same path)
	int ref_count;        // How many people are currently using the file (for garbage collection)
	int file_size;
	time_t last_use;      // The last time anyone called close on the file - will be used to determine last use & hence priority in the cache when space is needed
	void* data;           // Points to the memory that holds the contents of the file
};


//One of these for every client - will point to a cache page. Keeps track of where the client is in the file
struct file_cached {
  int position;                   // How many bytes have we written so far
	struct cache_page* cache_page;  // The pointer to the cache page the cfd has open
};


//File open from disk, not copied into cache
struct file_not_cached {
	FILE *open_ptr; // Keeps track of where they're reading in the file and when we reach the file end
	int file_size;
	int taken;      // Boolean; if this space is taken by an open, not cached file
};


// Holds the vector of files open but not in the cache
struct not_cached_mgr {
	int size;
	struct file_not_cached* files_not_cached;
};


// Holds the vector of cache pages
struct cache_page_mgr {

};


// Holds the vector of the actual files that have been opened and copied into the cache (at least I'm pretty sure that was the thought process)
struct cached_mgr { 

};


// The manager of everything - our cache
struct cache {
	pthread_mutex_t cache_mu;               
	struct not_cached_mgr not_cached_mgr;
	struct client_mgr client_mgr;
//	struct cache_page_mgr;
//	struct cached_mgr;
	int size;
	int bytes_used;
};



/* Set up */


static struct cache cache;

// Initializes the above structures
void cache_init(int size) //Maybe should return a number
{
  pthread_mutex_init(&cache.cache_mu,NULL);
  cache.size = size; // starting cache size
  cache.bytes_used = 0;
  
  cache.client_mgr.client_size = 1; // 1 item in the list of clients to start
  cache.client_mgr.clients = calloc(sizeof(struct cfd),cache.client_mgr.client_size); //allocate's memory for 1 client and sets all values to 0
  
  cache.not_cached_mgr.size = 1; // 1 item in the list of not cached files to start
  cache.not_cached_mgr.files_not_cached = calloc(sizeof(struct file_not_cached),cache.not_cached_mgr.size); //allocate's memory for 1 file (not cached) and sets all values to 0
  
  //TODO: cache.cached_mgr...
}



/* LOWEST LEVEL METHODS */


// Define a filesize for a version of a file that's not cached
static int filesize_v_not_cached(struct cfd* client)
{
	struct file_not_cached* p = (struct file_not_cached*) client->interface;
	return p->file_size;
}


static int send_v_not_cached(struct cfd* client, int client_fd, int n_bytes)
{
	//Unlocked for performance but there's no danger here - mix of local variables and variables owned by the one thread
	struct file_not_cached* p = (struct file_not_cached*) client->interface; //up to the caller to know how many bytes is left in the file
	int our_fd = fileno(p->open_ptr); //Converts a FILE* into a file descriptor which we then pass to sendfile
	if(our_fd == -1) return -1;
  // Unlock so that other threads can send in parallel
  pthread_mutex_unlock( &cache.cache_mu );
	// Instead of calling write() which would require a bunch of extra steps, we're using sendfile()
	int ret = sendfile(client_fd,our_fd,NULL,n_bytes); //This reads n bytes from one file descriptor (our_fd) into the other (client_fd)
  // Re-lock before returning otherwise the unlock higher up won't make sense
  pthread_mutex_lock( &cache.cache_mu );
  return ret;
}


static int close_v_not_cached(struct cfd* client)
{
	struct file_not_cached* p = (struct file_not_cached*) client-> interface;
	fclose(p->open_ptr);

	//Reset state
	client->taken=0;
	p->taken=0;

	return 0;
}


static int setup_not_cached_file(struct cfd* cfd,char *file, int file_size,struct file_not_cached* fnc)
{
	FILE* f = fopen(file,"rb");
	if(!f) return -1;
	fnc->open_ptr = f;
	fnc->file_size=file_size;
	fnc->taken=1;

	cfd->interface=fnc; //points to the "file"
	cfd->filesize_ptr=filesize_v_not_cached; //so when file_size function is called, it will go to the version designed for not cached files
	cfd->taken=1;

	cfd->send_ptr=send_v_not_cached; //not cached version of send

	cfd->close_ptr=close_v_not_cached; //not cached version of close
	return cfd->id;
}


static int open_not_cached(struct cfd* cfd, char *file,int file_size)
{
	struct file_not_cached* curr = cache.not_cached_mgr.files_not_cached; //This will never be null
	struct file_not_cached* end = curr+cache.not_cached_mgr.size; //will be one past the end
	while (curr != end)
	{
		if (!(curr->taken))
		{
			return setup_not_cached_file(cfd,file,file_size,curr); //return -1 if unsuccessful
		}
		curr++;
	}
	// make a temp list of ints to remember what cfd points to us
	struct cfd** scratch = malloc( sizeof( struct cfd* ) * cache.not_cached_mgr.size );
	// populate scratch with a temp list of the file_not_cached positions.
	{
		int i = 0;
		struct file_not_cached* tmp = cache.not_cached_mgr.files_not_cached;
		struct cfd** scratch_curr = scratch;
		for( i = 0; i < cache.not_cached_mgr.size; i++ )
		{
			struct cfd* search_cfd = cache.client_mgr.clients;
			int count = cache.client_mgr.client_size;
			while( count-- )
			{
				if( search_cfd->interface == tmp )
				{
					*scratch_curr = search_cfd;
					++scratch_curr;
					break;
				}
				search_cfd++;
			}
			if( 0 == count )
			{
				/* If we can't find it, something is horribly wrong */
				free( scratch );
				return -1;
			}
			tmp++;
		}
	}

	//Didn't find one - make the list bigger
	//In real world, there would of course be a finite number of clients being handled, however for simplicity we'll let this list grow unbounded.
	struct file_not_cached* new_memory = realloc(cache.not_cached_mgr.files_not_cached,(sizeof(struct file_not_cached)*cache.not_cached_mgr.size)*2); //allocate's memory for 1 file
	if (new_memory == cache.not_cached_mgr.files_not_cached)
	{
		// free the scratch before returning
		free( scratch );
		printf("Failure to reallocate not-cached list");
		return -1;
	}
	cache.not_cached_mgr.files_not_cached = new_memory;

	// using scratch created above, re populate the cfd->interface of all affected cfds
	{
		struct cfd** c = scratch;
		struct file_not_cached* f = cache.not_cached_mgr.files_not_cached;
		int i = 0;
		for( i = 0; i < cache.not_cached_mgr.size; i++ )
		{
			(*c)->interface = f;
			f++;
			c++;
		}
	}
	// free the temp buffer
	free( scratch );
 
	// only after we have manipulated the list will we update size
	cache.not_cached_mgr.size*=2;

	curr = cache.not_cached_mgr.files_not_cached+cache.not_cached_mgr.size/2;
	memset(curr,0,(cache.not_cached_mgr.size/2)*sizeof(struct file_not_cached));
	return setup_not_cached_file(cfd,file,file_size,curr); //return -1 if unsuccessful
}

static int open_cached(struct cfd* cfd, char *file,int file_size)
{
	//TODO: this.
	return -1;
}

//TODO: static int filesize_v_cached(struct cfd* client), static int send_v_cached, static int close_v_cached, static int setup_cached_file...

static struct file_cached* find_in_cache(char* file)
{
	//Look through cache for matching inode (i.e. file is already open in cache)
	//Return a pointer to the file_cached
	//TODO: This
	return NULL;
}

static int join(struct cfd* cfd, struct file_cached* fc)
{
	//Link the cfd to the file that's already cached using fc
	//Return the id of the cfd, or -1 if error
	//TODO: This
	return -1;
}

static int check_for_room(int file_size)
{
	//Check for room for file_size bytes in cache - if no room, check for old files in cache that are not currently in use and if you find those, pop them out and return 1 saying there is now room
	// Else 0 if there's no room and you cannot make room
	//TODO: This
	return 0;
}


/* Upper Level Functions */


// Determine what type of file is being asked for! Return the cfd ID
static int assign_file(struct cfd* cfd, char *file)
{
	//Check if file is already cached - if yes, link the cfd to the already-cached file
	struct file_cached* fc = find_in_cache(file); //return a pointer to the file cached
	if (fc)	return join(cfd,fc);

	//If file is not in cache
	FILE* f = fopen(file,"rb"); //r=read only mode, b is a specificity for windows systems
	if(!f)
	{
		return -1;// MAKE SURE THIS IS HANDLED AS A 404 "File not found"
	}
	if(-1 == fseek(f,0,SEEK_END)) return -1;  //Returns 0 if successful, -1 if there was corruption
	int file_size = ftell(f); //tells the current position which is = to the number of bytes since we're pointing to the end of the file from the line above (FILE* rememebers state)
	//Use file_size to determine if it fits int the cache

	fclose(f);
	//is there room in the cache, and call the right function
	//TODO: WRITE IF; Do the caching later - now assume cache is full and go directly to file
	int cache_has_room = check_for_room(file_size); //0 if cache is full & no room - hence need to open file outside of cache
	if (!cache_has_room)
	{
		return open_not_cached(cfd,file,file_size);
	}
	return open_cached(cfd,file,file_size);
}



int cache_open(char *file)
{
	//Lock!
	pthread_mutex_lock(&cache.cache_mu);

	//determine if there's a free slot - if yes, fill it, if no expand the vector (realloc),

	//Find CFD
	int i = 0;
	struct cfd* curr = cache.client_mgr.clients; //This will never be null
	struct cfd* end = curr+cache.client_mgr.client_size; //will be one past the end
	while (curr != end)
	{
		if (!(curr->taken))
		{
			curr->id = i; //set the ID of the new
			int ret = assign_file(curr, file); //returns -1 if unsuccessful or ID number of successful assignment
			pthread_mutex_unlock(&cache.cache_mu);
			return ret;
		}
		curr++;
		i++;
	}
	//Didn't find one - make the list bigger
	//In real world, there would of course be a finite number of clients being handled, however for simplicity we'll let this list grow unbounded.
	struct cfd* temp = realloc(cache.client_mgr.clients,(sizeof(struct cfd)*cache.client_mgr.client_size)*2); //allocate's memory for 1 client
	if (temp == cache.client_mgr.clients)
	{
		printf("Failure to reallocate client list");
		pthread_mutex_unlock(&cache.cache_mu);
		return -1;
	}
	cache.client_mgr.clients = temp;
	cache.client_mgr.client_size*=2;
	curr = cache.client_mgr.clients+cache.client_mgr.client_size/2;
	memset(curr,0,(cache.client_mgr.client_size/2)*sizeof(struct cfd));
	curr->id=i;
	int ret = assign_file(curr,file); //insert at the first new slot (reminder to me: this works since arrays start at 0 so)
	pthread_mutex_unlock(&cache.cache_mu);
	return ret;
}


int cache_send(int cfd, int client, int n)
{
	// Don't want global lock since we don't want to bottleneck
	pthread_mutex_lock(&cache.cache_mu);
	struct cfd* curr = cache.client_mgr.clients; //This will never be null
	struct cfd* end = curr+cache.client_mgr.client_size; //will be one past the end
	while(curr != end)
	{
		if(curr->id==cfd)
		{
			int ret = curr->send_ptr(curr,client,n);
			pthread_mutex_unlock(&cache.cache_mu);
			return ret;
		}
    curr++;
	}
	pthread_mutex_unlock(&cache.cache_mu);
	return -1; //didn't find that id
}


int cache_filesize(int cfd)
{
	pthread_mutex_lock(&cache.cache_mu);
	//find the cfd
	struct cfd* curr = cache.client_mgr.clients; //This will never be null
	struct cfd* end = curr+cache.client_mgr.client_size; //will be one past the end
	while(curr != end)
	{
		if(curr->id==cfd)
		{
			int ret = curr->(curr);
			pthread_mutex_unlock(&cache.cache_mu);
			return ret;
		}
    curr++;
	}
	pthread_mutex_unlock(&cache.cache_mu);
	return -1; //didn't find that id
}


int cache_close(int cfd)
{
	pthread_mutex_lock(&cache.cache_mu);

	//find the cfd
	struct cfd* curr = cache.client_mgr.clients; //This will never be null
	struct cfd* end = curr+cache.client_mgr.client_size; //will be one past the end
	while(curr != end)
	{
		if(curr->id==cfd)
		{
			int ret = curr->close_ptr(curr);
			pthread_mutex_unlock(&cache.cache_mu);
			return ret;
		}
    curr++;
	}
	pthread_mutex_unlock(&cache.cache_mu);
	return -1;
}
