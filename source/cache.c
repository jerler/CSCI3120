/*
 * cache.c
 *
 *  Created on: Mar 21, 2017
 *      Author: Julie Morrissey - B00592466
 */
#include <stdio.h>
#include <pthread.h>
#include <sys/stat.h> //for inode

// Implement the cache as a vector. Allocate an initial amount of data and as people add we
// double. We won't ever decrease the size of the allocated memory

struct cfd; // for the function pointer

//Define function pointer
typedef int (*cache_filesize_fptr) (struct cfd*); // a type of function pointer - int is the return type
typedef int (*cache_send_fptr) (struct cfd*);

// Every client gets a cfd
struct cfd {
	int id; //The ID of the client - this is what gets returned to the client
	void* interface; //points to either a file_cached or a file_uncached
	//function pointers
	cache_filesize_fptr filesize_ptr;
	cache_send_fptr send_ptr;
	int taken; // boolean; if this space is taken by a client
};

// one of these for every entry in the cache
struct file_cached {
	ino_t inode; //The unique identifier of the file
	int ref_count; // How many people are currently using the file (for garbage collection)
	int file_size;
};

//File open from disk, not copied into cache
struct file_not_cached {
	FILE* open_ptr; //keeps track of where they're reading in the file and when the reach the file end
	int file_size;
	int taken; //boolean; if this space is taken by an open not cached file
};

struct client_mgr {
	int client_size;
	struct cfd* clients;
};

struct not_cached_mgr {
	int size;
	struct file_not_cached* files_not_cached;
};

struct cached_mgr {

};

// Our cache of file_cached
struct cache {
	struct file_cached* files_cached; // points to a bunch of files (decided dynamically)
	pthread_mutex_t cache_mu;
	struct not_cached_mgr;
	struct client_mgr;
	int size;
	int bytes_used;
};



static struct cache cache;


static int setup_not_cached_file(struct cfd* cfd,char *file, int file_size,struct file_not_cached* fnc)
{
	FILE* f = fopen(file,"rb");
	if(!f) return -1;
	fnc.open_ptr = f;
	fnc.file_size=file_size;
	fnc.taken=1;

	cfd.interface=fnc; //points to the "file"
	cfd.filesize_ptr=filesize_v_not_cached; //so when filesize function is called, it will go to the version designed for not cached files
	cfd.taken=1;
	return cfd.id;
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
	//Didn't find one - make the list bigger
	//In real world, there would of course be a finite number of clients being handled, however for simplicity we'll let this list grow unbounded.
	struct file_not_cached* temp = realloc(cache.not_cached_mgr.files_not_cached,(sizeof(struct file_not_cached)*cache.not_cached_mgr.size)*2); //allocate's memory for 1 file
	if (temp == cache.not_cached_mgr.files_not_cached)
	{
		printf("Failure to reallocate not-cached list");
		return -1;
	}
	cache.not_cached_mgr.files_not_cached = temp;
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

//Define a filesize for a version of a file that's not cached
static int filesize_v_not_cached(struct cfd* client)
{
	struct file_not_cached* p = (struct file_not_cached*) cfd->interface;
	return p->filesize;
}

int assign_file(struct cfd* cfd, char *file) //return CFD ID
{
	FILE* f = fopen(file,"rb"); //r=read only mode, b is a specificity for windows systems
	if(!f)
	{
		return -1;// MAKE SURE THIS IS HANDLED AS A 404 "File not found"
	}
	if(-1 == fseek(f,0,SEEK_END)) return -1  //Returns 0 if successful, -1 if there was corruption
	int file_size = ftell(f); //tells the current position which is = to the number of bytes since we're pointing to the end of the file from the line above (FILE* rememebers state)
	//Use file_size to determine if it fits int the cache

	fclose(f);
	//is there room in the cache, and call the right function
	//TODO: WRITE IF; Do the caching later - now assume cache is full and go directly to file
	int cache_full = 1; //1 if cache is full - normally want this to be 0;
	if (cache_full)
	{
		return open_not_cached(cfd,file,file_size);
	}
	return open_cached(cfd,file,file_size);
}


//allocate our initial memory into a "vector"
void cache_init(int size) //Maybe should return a number
{
	pthread_mutex_init(&cache.cache_mu);
	cache.size = size; // starting cache size
	cache.bytes_used = 0;
	//TODO: initialize these
	cache.files_cached = NULL;
	cache.files_not_cached = NULL;

	cache.client_mgr.client_size = 1; // 1 item in the list of clients to start
	cache.client_mgr.clients = calloc(sizeof(struct cfd)*cache.client_mgr.client_size); //allocate's memory for 1 client and sets all values to 0

	cache.not_cached_mgr.size = 1; // 1 item in the list of not cached files to start
	cache.not_cached_mgr.files_not_cached = calloc(sizeof(struct file_not_cached)*cache.not_cached_mgr.size); //allocate's memory for 1 file (not cached) and sets all values to 0


}

int cache_open(char *file)
{
	//determine if there's a free slot - if yes, fill it, if no expand the vector (realloc),

	//Find CFD
	int i = 0;
	struct cfd* curr = cache.client_mgr.clients; //This will never be null
	struct cfd* end = curr+cache.client_mgr.client_size; //will be one past the end
	while (curr != end)
	{
		if (!(curr->taken))
		{
			curr.id = i; //set the ID of the new
			return assign_file(curr, file); //returns -1 if unsuccessful or ID number of successful assignment
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
		return -1;
	}
	cache.client_mgr.clients = temp;
	cache.client_mgr.client_size*=2;
	curr = cache.client_mgr.clients+cache.client_mgr.client_size/2;
	memset(curr,0,(cache.client_mgr.client_size/2)*sizeof(struct cfd));
	curr.id=i;
	return assign_file(curr,file); //insert at the first new slot (reminder to me: this works since arrays start at 0 so)
}

int cache_send(int cfd, int client, int n)
{

}

int cache_filesize(int cfd)
{
	//find the cfd
	struct cfd* curr = cache.client_mgr.clients; //This will never be null
	struct cfd* end = curr+cache.client_mgr.client_size; //will be one past the end
	while(curr != end)
	{
		if(curr.id==cfd) return curr->filesize_ptr(curr);
	}
	return -1; //didn't find that id
}

int cache_close(int cfd)
{

}
