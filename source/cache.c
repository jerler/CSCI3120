
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "list.h"
#include <sys/stat.h> //for inode
// This is just so that I can compile on OSX and it doesn't have sendfile.
// THE MAKEFILE MUST HAVE -DHAS_SENDFILE TO WORK!!!
#ifdef HAS_SENDFILE
	#include <sys/sendfile.h> //SYSTEM DEPENDENT - USE LINUX
#endif



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


// One of these for every entry in the cache
struct cache_page {
	ino_t inode;          // The unique identifier of the file (My understanding that inodes identify files even if not in same path)
	int ref_count;        // How many people are currently using the file (for garbage collection)
	int file_size;
	time_t last_use;      // The last time anyone called close on the file - will be used to determine last use & hence priority in the cache when space is needed
	char* data;           // Points to the memory that holds the contents of the file
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
};



// The manager of everything - our cache
struct cache {
	pthread_mutex_t cache_mu;               
	struct client_mgr client_mgr;
	struct link_list* cache_page_list;
	struct link_list* not_cached_list;
	struct link_list* cached_list;
	int max_bytes_size;
};

// This will only be used for the list that contains cache_pages (aka cache_page_list)
// Needs the extra step to free the memory it references
static void page_dtor(void* p)
{
	struct cache_page* page = p;
	free(page->data);
	free(page);
}

/* Set up */

static struct cache cache;

// Initializes the above structures
void cache_init(int size) //Maybe should return a number
{
  pthread_mutex_init(&cache.cache_mu,NULL);
  cache.max_bytes_size = size; // starting cache size
  
  cache.client_mgr.client_size = 1; // 1 item in the list of clients to start
  cache.client_mgr.clients = calloc(sizeof(struct cfd),cache.client_mgr.client_size); //allocate's memory for 1 client and sets all values to 0
  
  cache.not_cached_list=link_list_init(free); //so when you want to get rid of a node / the whole linked list it'll just free the memory
  cache.cached_list=link_list_init(free);
  cache.cache_page_list=link_list_init(page_dtor);
}



/* LOWEST LEVEL METHODS */


static int filesize_v_cached(struct cfd* client)
{
	struct file_cached* p = (struct file_cached*) client->interface;
	return p->cache_page->file_size;
}

// Define a filesize for a version of a file that's not cached
static int filesize_v_not_cached(struct cfd* client)
{
	struct file_not_cached* p = (struct file_not_cached*) client->interface;
	return p->file_size;
}


static int send_v_cached(struct cfd* client, int client_fd, int n_bytes)
{
	struct file_cached* p = (struct file_cached*) client->interface;
	char* src = p->cache_page->data + p->position;
	int actually_written;
	const int bytes_left = p->cache_page->file_size - p->position;
	if( bytes_left < n_bytes ) {
		n_bytes = bytes_left;
	}
	pthread_mutex_unlock( &cache.cache_mu );
	actually_written = write( client_fd, src, n_bytes );
	pthread_mutex_lock( &cache.cache_mu );
	
	if( -1 == actually_written ) {
		return -1;
	}

	p->position += actually_written;
	return actually_written;

}

static int send_v_not_cached(struct cfd* client, int client_fd, int n_bytes)
{
	//Unlocked for performance but there's no danger here - mix of local variables and variables owned by the one thread
	struct file_not_cached* p = (struct file_not_cached*) client->interface; //up to the caller to know how many bytes is left in the file
	int our_fd = fileno(p->open_ptr); //Converts a FILE* into a file descriptor which we then pass to sendfile
	if(our_fd == -1) return -1;
	int ret = -1; // This is to catch the HAS_SENDFILE case below
	// Unlock so that other threads can send in parallel
	pthread_mutex_unlock( &cache.cache_mu );
	// Instead of calling write() which would require a bunch of extra steps, we're using sendfile()
	#ifdef HAS_SENDFILE
		ret = sendfile(client_fd,our_fd,NULL,n_bytes); //This reads n bytes from one file descriptor (our_fd) into the other (client_fd)
	#endif
	// Re-lock before returning otherwise the unlock higher up won't make sense
	pthread_mutex_lock( &cache.cache_mu );
	return ret;
}


static int close_v_cached(struct cfd* client)
{
    struct file_cached* p = (struct file_cached*) client-> interface;

	//Reset state
	client->taken=0;

	//Decrement refcount, and update last time used
	p->cache_page->ref_count--;
	p->cache_page->last_use = time( NULL );

	link_list_remove(cache.cached_list,p);
	return 0;
}

static int close_v_not_cached(struct cfd* client)
{
	struct file_not_cached* p = (struct file_not_cached*) client-> interface;
	fclose(p->open_ptr);

	//Reset state
	client->taken=0;
	link_list_remove(cache.not_cached_list,p);

	return 0;
}

static int load_page(char* file, int file_size, struct cache_page* page)
{
	struct stat stat;
	FILE* f = fopen(file,"rb");
	if(!f) return 0;
	page->file_size=file_size;
	page->data = malloc(file_size);
	if(!page->data)
	{
		fclose(f);
		return 0;
	}

	if(fread(page->data,file_size,1,f) != 1)
	{
		free(page->data);
		page->data=NULL;
		fclose(f);
		return 0;
	}
	if(fstat(fileno(f),&stat)!=0)
	{
		free(page->data);
		page->data=NULL;
		fclose(f);
		return 0;
	}
	page->inode=stat.st_ino;
	fclose(f);
	page->last_use=0;
	page->ref_count=1;

	printf("File of size %d cached.\n",file_size);
	return 1;
}


static struct cache_page* add_to_cache(char* file,int file_size)
{
	struct cache_page* temp = calloc(sizeof(struct cache_page),1);
	if(!temp) return NULL;

	if(!link_list_add_front(cache.cache_page_list,temp))
	{
		free(temp);
		return NULL;
	}

	if(!load_page(file,file_size,temp))
	{
		link_list_remove(cache.cache_page_list,temp);
		return NULL;
	}

	return temp;
}


static int setup_not_cached_file(struct cfd* cfd,char *file, int file_size,struct file_not_cached* fnc)
{
	FILE* f = fopen(file,"rb");
	if(!f) return -1;
	fnc->open_ptr = f;
	fnc->file_size=file_size;

	cfd->interface=fnc; //points to the "file"
	cfd->filesize_ptr=filesize_v_not_cached; //so when file_size function is called, it will go to the version designed for not cached files
	cfd->taken=1;

	cfd->send_ptr=send_v_not_cached; //not cached version of send

	cfd->close_ptr=close_v_not_cached; //not cached version of close
	return cfd->id;
}

static int setup_cached_file(struct cfd* cfd, char *file, int file_size, struct file_cached* fc)
{
	fc->cache_page = add_to_cache(file,file_size);
	if(!fc->cache_page) return -1;

	fc->position = 0;

	cfd->interface=fc; //points to the "file"
	cfd->filesize_ptr=filesize_v_cached; //so when file_size function is called, it will go to the version designed for not cached files
	cfd->taken=1;

	cfd->send_ptr=send_v_cached; //not cached version of send

	cfd->close_ptr=close_v_cached; //not cached version of close

	return cfd->id;
}


static int open_not_cached(struct cfd* cfd, char *file,int file_size)
{
	struct file_not_cached* temp = calloc(sizeof(struct file_not_cached),1); //get memory needed to add file
	if(!temp) return -1;

	if(!link_list_add_front(cache.not_cached_list,temp))
	{
		free(temp);
		return -1;
	}

	return setup_not_cached_file(cfd,file,file_size,temp); //return -1 if unsuccessful
}

static int open_cached(struct cfd* cfd, char *file,int file_size)
{

	struct file_cached* temp = calloc(sizeof(struct file_cached),1);
	if (!temp) return -1;

	if(!link_list_add_front(cache.cached_list,temp))
	{
		free(temp);
		return -1;
	}

	int cfd_id = setup_cached_file(cfd,file,file_size,temp);
	if (cfd_id == -1)
	{
		link_list_remove(cache.cached_list,temp);
		return -1;
	}
	return cfd_id;
}


static unsigned int find_by_inode( void* context, void* item ) //to pass to our link_list_find method to let it know when it has found what it's looking for
{
	ino_t* looking_for = context;
	struct cache_page* page = item;

	return *looking_for == page->inode;
}

static struct cache_page* find_in_cache(char* file)
{
	struct stat s;
	if(-1 == stat(file,&s)) return NULL;

	ino_t id = s.st_ino;
	return link_list_find(cache.cache_page_list,find_by_inode,&id); //Will keep calling find_by_inode until it finds what it's looking for or reach end (return NULL)

}

static int join(struct cfd* cfd, struct cache_page* cp)
{
	//Link the cfd to the file that's already cached using fc
	//Return the id of the cfd, or -1 if error
	struct file_cached* temp = calloc(sizeof(struct file_cached),1);
	if(!temp) return -1;

	if(!link_list_add_front(cache.cached_list,temp))
	{
		free(temp);
		return -1;
	}

	temp->position=0; //redundant but explicit
	temp->cache_page = cp;
	cp->ref_count++;
	cfd->close_ptr = close_v_cached;
	cfd->filesize_ptr = filesize_v_cached;
	cfd->send_ptr = send_v_cached;
	cfd->interface = temp;
	cfd->taken=1;

	return cfd->id;
}

static void count_bytes( void* context, void* item )
{
	int* counter = context;
	struct cache_page* cp = item;

	*counter+=cp->file_size;
}

static void count_freeable( void* context, void* item )
{
	int* counter = context;
	struct cache_page* cp = item;

	if(!cp->ref_count)
	{
		*counter+=cp->file_size;
	}
}

static unsigned int find_first_freeable( void* context, void* item )
{
	struct cache_page* cp = item;

	return cp->ref_count==0;
}

static void calc_oldest_time( void* context, void* item )
{
	time_t* oldest = context;
	struct cache_page* cp = item;

	if (cp->last_use < *oldest) *oldest=cp->last_use;
}

static unsigned int find_oldest_page( void* context, void* item)
{
	time_t* oldest = context;
	struct cache_page* cp = item;

	return cp->last_use==*oldest;
}


//Check for room for file_size bytes in cache
//if no room, check for old files in cache that are not currently in use and if you find those, pop them out and return 1 saying there is now room
// Else 0 if there's no room and you cannot make room
static int try_make_room(int file_size)
{
	int bytes_used = 0;
	link_list_foreach(cache.cache_page_list,count_bytes,&bytes_used); //stores bytes used into "bytes used"

	int bytes_free = cache.max_bytes_size-bytes_used;
	if(file_size<=bytes_free) return 1;

	int bytes_freeable = 0;
	link_list_foreach(cache.cache_page_list,count_freeable,&bytes_freeable);
	if((bytes_free+bytes_freeable)<file_size) return 0;

	while(1)
	{

		//find the time of any unused page
		struct cache_page* cp = link_list_find(cache.cache_page_list,find_first_freeable,NULL);
		time_t first_time = cp->last_use;

		time_t oldest_time = first_time;
		//find the time of the oldest unused page
		link_list_foreach(cache.cache_page_list,calc_oldest_time,&oldest_time);

		//find the oldest unused page
		cp = link_list_find(cache.cache_page_list,find_oldest_page,&oldest_time);

		//remove that page
		link_list_remove(cache.cache_page_list,cp);
		printf("File of size %d evicted\n",cp->file_size);

		//Now is there enough room?
		bytes_used = 0;
		link_list_foreach(cache.cache_page_list,count_bytes,&bytes_used); //stores bytes used into "bytes used"
		bytes_free = cache.max_bytes_size-bytes_used;
		if(file_size<=bytes_free) break;
	}
	return 1;
}


/* Upper Level Functions */


// Determine what type of file is being asked for! Return the cfd ID
static int assign_file(struct cfd* cfd, char *file)
{
	//Check if file is already cached - if yes, link the cfd to the already-cached file
	struct cache_page* fc = find_in_cache(file); //return a pointer to the file cached
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
	int cache_has_room = try_make_room(file_size); //0 if cache is full & no room - hence need to open file outside of cache
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
			int ret = curr->filesize_ptr(curr);
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


void cache_destroy()
{
	link_list_destroy(cache.not_cached_list);
	link_list_destroy(cache.cached_list);
	link_list_destroy(cache.cache_page_list);
	free(cache.client_mgr.clients);
}


