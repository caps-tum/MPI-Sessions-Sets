/*This file is part of the MPI Sessions library.
 * 
 *Author : Vishnu Anilkumar Suma
 *Date   : 15/03/2019
 *
 *This file, kvs.c implements the routines to interact with the key-value store in FLUX
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <kvs.h>
#include <mpisessions.h>
#include <mpi.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>

#define KVS_MAX_SET_NAME_LENGTH 50
#define KVS_VERSION_UPDATE 31173 //Or anything else really, I should be the only one still using MPI_COMM_WORLD at that point, if not I probably need to make a copy anyway

struct KVS_head{
	int num_entries;
	int version;
	sem_t sem;
};

struct KVS_entry{
	int key_length;
	char key[KVS_MAX_SET_NAME_LENGTH];
	int version;
	int mem_version;
	int num_ranks;
	int num_updates;
	int mem_ranks;
	int mem_updates;
};

int *mem_version;
int *mem_ranks;
int *mem_updates;

const char const *head_identifier = "_kvs_head";
const char const *entries_identifier = "_kvs_entries";

struct KVS_head *head_baseptr;
struct KVS_entry *entries_baseptr;
int **ranks_baseptr;
int **updates_baseptr;

void debug_print_KVS(bool isSpawned){
	char to_print[2048]; //Quick'n'dirty, should be enough
	char *pos = to_print;
	
	for(int r = 0; r < mpi_world_size; r++){
		if(mpi_world_rank==r){
			sem_wait(&(head_baseptr->sem));
			
			pos += sprintf(pos, "KVS %i: Debug printout, isSpawnend: %i, numEntries: %i, version: %i\n", mpi_world_rank, isSpawned, head_baseptr->num_entries, head_baseptr->version);
			
			for(int i = 0; i < head_baseptr->num_entries; i++){
				pos += sprintf(pos, 
					"entry: %i, key_length: %i, key: %s, version: %i, nranks: %i, memranks: %i\n", 
					i, entries_baseptr[i].key_length, entries_baseptr[i].key, entries_baseptr[i].version, entries_baseptr[i].num_ranks, entries_baseptr[i].mem_ranks);
				pos += sprintf(pos, "ranks: ");
				for(int j = 0; j < entries_baseptr[i].num_ranks; j++){
					int val = ranks_baseptr[i][j];
					pos += sprintf(pos, "%i ", val);
				}
				pos += sprintf(pos, "\n");
			}
		
			pos += sprintf(pos, "\n");
			
			printf("%s", to_print);
			sem_post(&(head_baseptr->sem));
		}
	}
}

//Very primitive, find better one
int hash(const char* s, int m){
	int r = 7;
	while(*s!=0){
		r = r*31 + *s;
		s++;
	}
	return abs(r) % m;
}

int locate_set(const char *key){
	int pos = hash(key, head_baseptr->num_entries);
	
	int n = pos;
	do{
		if(strcmp(key, entries_baseptr[n].key)==0)
			return n;
		n = (n + 1) % head_baseptr->num_entries;
	}while(n!=pos);
	
	printf("KVS %i: DID NOT FIND\n", mpi_world_rank);
	return -1;
}

int num_digits(int n){
	int ret = 1;
	while((n/=10) != 0) ret++;
	return ret;
}

//Build the name to identify a shared memory region for ranks/updates
//Out has to be freed by user
void construct_name(int setnumber, const char *postfix, char **out){
	int nsetnumber = num_digits(setnumber);
	
	*out = malloc(nsetnumber + strlen(postfix) + strlen(program_identifier) + 5);
	sprintf(*out, "%s_%i_%s", program_identifier, setnumber, postfix);
}

void open_memory_block(int setnumber, const char *postfix, int **memory, size_t size){
	char *tmp;
	construct_name(setnumber, postfix, &tmp);
	int fd;
	if((fd = shm_open(tmp, O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)) == -1){
		printf("KVS %i: shm_open failed, exiting\n", mpi_world_rank);
		perror("shm_open encountered: ");
		exit(-1);
	}
	if((*memory = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == ((void *) -1)){
		printf("KVS %i: mmap failed, exiting\n", mpi_world_rank);
		exit(-1);
	}
	close(fd);
	free(tmp);
}

void allocate_memory_block(int setnumber, const char *postfix, int **memory, size_t size){
	char *tmp;
	construct_name(setnumber, postfix, &tmp);
	int fd;
	if((fd = shm_open(tmp, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)) == -1){
		printf("KVS %i: shm_open failed, exiting\n", mpi_world_rank);
		perror("shm_open encountered: ");
		exit(-1);
	}
	ftruncate(fd, size);
	if((*memory = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == ((void *) -1)){
		printf("KVS %i: mmap failed, exiting\n", mpi_world_rank);
		exit(-1);
	}
	memset(*memory, 0, size); 
	close(fd);
	free(tmp);
}

void open_ranks_and_updates(){
	ranks_baseptr = malloc(head_baseptr->num_entries * sizeof(int*));
	updates_baseptr = malloc(head_baseptr->num_entries * sizeof(int*));

	for(int i = 0; i < head_baseptr->num_entries; i++){
		open_memory_block(i, "ranks", &(ranks_baseptr[i]), 
			entries_baseptr[i].mem_ranks * sizeof(int));
		open_memory_block(i, "updates", &(updates_baseptr[i]), 
			entries_baseptr[i].mem_updates * sizeof(int));
	}
}

void allocate_ranks_and_updates(){
	ranks_baseptr = malloc(head_baseptr->num_entries * sizeof(int*));
	updates_baseptr = malloc(head_baseptr->num_entries * sizeof(int*));
	
	for(int i = 0; i < head_baseptr->num_entries; i++){
		//I'm mapping enough space for all processes, according to SO this is only virtual memory TODO: Check if it isn't to much anyway
		int size = mpi_world_size;
		
		allocate_memory_block(i, "ranks", &(ranks_baseptr[i]), size * sizeof(int));
		allocate_memory_block(i, "updates", &(updates_baseptr[i]), size * sizeof(int));
		
		//Save size information
		entries_baseptr[i].mem_ranks = size;
		entries_baseptr[i].mem_updates = size;
	}
}

//Works also for blocks from open_memory_block
void deallocate_memory_block(int setnumber, const char *postfix, int *memory, size_t size){
	munmap(memory, size);
	char *tmp;
	construct_name(setnumber, postfix, &tmp);
	strcpy(tmp, program_identifier);
	strcat(tmp, "_kvs");
	shm_unlink(tmp);
	free(tmp);
}

//Works also for if open_ranks_and_updates was used
void deallocate_ranks_and_updates(){
	for(int i = 0; i < head_baseptr->num_entries; i++){
		deallocate_memory_block(i, "ranks", ranks_baseptr[i], entries_baseptr[i].mem_ranks * sizeof(int));
		deallocate_memory_block(i, "updates", updates_baseptr[i], entries_baseptr[i].mem_updates * sizeof(int));
	}
	
	free(ranks_baseptr);
	free(updates_baseptr);
}

void open_KVS_head(){
	char *tmp = malloc(strlen(program_identifier) + strlen(head_identifier) + 1);
	strcpy(tmp, program_identifier);
	strcat(tmp, head_identifier);
	int fd;
	if((fd = shm_open(tmp, O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)) == -1){
		printf("KVS %i: shm_open failed, exiting\n", mpi_world_rank);
		perror("shm_open encountered: ");
		exit(-1);
	}
	size_t size = sizeof(struct KVS_head);
	if((head_baseptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == ((void *) -1)){
		printf("KVS %i: mmap failed, exiting\n", mpi_world_rank);
		perror("mmap encountered: ");
		exit(-1);
	}
	close(fd);
	free(tmp);
}

void allocate_KVS_head(){
	char *tmp = malloc(strlen(program_identifier) + strlen(head_identifier) + 1);
	strcpy(tmp, program_identifier);
	strcat(tmp, head_identifier);
	int fd;
	if((fd = shm_open(tmp, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)) == -1){
		printf("KVS %i: shm_open failed, exiting\n", mpi_world_rank);
		perror("shm_open encountered: ");
		exit(-1);
	}
	size_t size = sizeof(struct KVS_head);
	ftruncate(fd, size);
	if((head_baseptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == ((void *) -1)){
		printf("KVS %i: mmap failed, exiting\n", mpi_world_rank);
		perror("mmap encountered: ");
		exit(-1);
	}
	memset(head_baseptr, 0, size);
	close(fd);
	free(tmp);
}

void open_KVS_entries(){
	char *tmp = malloc(strlen(program_identifier) + strlen(entries_identifier) + 1);
	strcpy(tmp, program_identifier);
	strcat(tmp, entries_identifier);
	int fd;
	if((fd = shm_open(tmp, O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)) == -1){
		printf("KVS %i: shm_open failed, exiting\n", mpi_world_rank);
		perror("shm_open encountered: ");
		exit(-1);
	}
	size_t size = sizeof(struct KVS_entry) * head_baseptr->num_entries;
	if((entries_baseptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == ((void *) -1)){
		printf("KVS %i: mmap failed, exiting\n", mpi_world_rank);
		perror("mmap encountered: ");
		exit(-1);
	}
	close(fd);
	free(tmp);
}

void allocate_KVS_entries(){
	char *tmp = malloc(strlen(program_identifier) + strlen(entries_identifier) + 1);
	strcpy(tmp, program_identifier);
	strcat(tmp, entries_identifier);
	
	int fd;
	if((fd = shm_open(tmp, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)) == -1){
		printf("KVS %i: shm_open failed, exiting\n", mpi_world_rank);
		perror("shm_open encountered: ");
		exit(-1);
	}
	size_t size = sizeof(struct KVS_entry) * head_baseptr->num_entries;
	ftruncate(fd, size);
	if((entries_baseptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == ((void *) -1)){
		printf("KVS %i: mmap failed, exiting\n", mpi_world_rank);
		perror("mmap encountered: ");
		exit(-1);
	}
	memset(entries_baseptr, 0, size);
	close(fd);
	free(tmp);
}

void deallocate_KVS_head(){
	munmap(head_baseptr, sizeof(struct KVS_head));
	char *tmp = malloc(strlen(program_identifier) + strlen(head_identifier) + 1);
	strcpy(tmp, program_identifier);
	strcat(tmp, head_identifier);
	shm_unlink(tmp);
	free(tmp);
}

void deallocate_KVS_entries(){
	if(munmap(entries_baseptr, sizeof(struct KVS_entry) * head_baseptr->num_entries) == -1){
		printf("KVS %i: munmap failed, exiting...\n", mpi_world_rank);
		perror("munmap encountered: ");
		exit(-1);
	}
	char *tmp = malloc(strlen(program_identifier) + strlen(entries_identifier) + 1);
	strcpy(tmp, program_identifier);
	strcat(tmp, entries_identifier);
	shm_unlink(tmp);
	free(tmp);
}

void KVS_intern_create_lock(){
	if(sem_init(&head_baseptr->sem, 1, 1) == -1){
		printf("KVS %i: sem_init failed, exiting...\n", mpi_world_rank);
		perror("sem_init encountered: ");
	}
}

void KVS_intern_destroy_lock(){
	if(sem_destroy(&(head_baseptr->sem)) == -1){
		printf("KVS %i: sem_destroy failed, exiting...\n", mpi_world_rank);
		perror("sem_destroy encountered: ");
	}
}

int KVS_intern_lock(){
	return sem_wait(&head_baseptr->sem);
}

int KVS_intern_unlock(){
	return sem_post(&head_baseptr->sem);
}

void update_local_memory_info(int setnumber){
	mem_version[setnumber] = entries_baseptr[setnumber].mem_version;
	mem_ranks[setnumber] = entries_baseptr[setnumber].mem_ranks;
	mem_updates[setnumber] = entries_baseptr[setnumber].mem_updates;
}

//TODO: Add error handling
int reallocate_memory(int **memory, size_t old_size, size_t new_size){
	if((*memory = mremap(*memory, old_size, new_size, MREMAP_MAYMOVE)) == (void*)-1){
		printf("KVS %i: mremap failed, exiting\n", mpi_world_rank);
		perror("mremap encountered: ");
		exit(-1);
	}
	return 0;
}

int update_memory(int setnumber){
	if(mem_version[setnumber] < entries_baseptr[setnumber].mem_version){
		//Code for the real update here
		reallocate_memory(&(ranks_baseptr[setnumber]), mem_ranks[setnumber] * sizeof(int), entries_baseptr[setnumber].mem_ranks * sizeof(int));
		reallocate_memory(&(updates_baseptr[setnumber]), mem_updates[setnumber] * sizeof(int), entries_baseptr[setnumber].mem_updates * sizeof(int));
		
		update_local_memory_info(setnumber);
	}
	
	return 0;
}

int rescale_memory_ranks(int setnumber, int old_mem, int new_mem){
	reallocate_memory(&(ranks_baseptr[setnumber]), old_mem*sizeof(int), new_mem*sizeof(int));
	
	entries_baseptr[setnumber].mem_version++;
	entries_baseptr[setnumber].mem_ranks = new_mem;
}

int rescale_memory_updates(int setnumber, int old_mem, int new_mem){
	reallocate_memory(&(updates_baseptr[setnumber]), old_mem*sizeof(int), new_mem*sizeof(int));
	
	entries_baseptr[setnumber].mem_version++;
	entries_baseptr[setnumber].mem_updates = new_mem;
}

//Put the first version of an entry in the KVS. 
void KVS_Put_initial(char *key, int num_ranks, int *ranks){
	KVS_intern_lock();
	
	int pos = hash(key, head_baseptr->num_entries);
	int n = pos;
	do{
		if(strlen(entries_baseptr[n].key) == 0){ //TODO: Pretty hacky check
			
			if(num_ranks > entries_baseptr[n].mem_ranks){
				rescale_memory_ranks(n, entries_baseptr[n].mem_ranks, num_ranks);
			}
		
			head_baseptr->version++;
			entries_baseptr[n].version = 1;
			entries_baseptr[n].num_ranks = num_ranks;
			
			strcpy(entries_baseptr[n].key, key);
			entries_baseptr[n].key_length = strlen(key);
			
			for(int i = 0; i < num_ranks; i++){
				ranks_baseptr[n][i] = ranks[i];
			}
			
			KVS_intern_unlock();
			return;
		}
		n = (n + 1) % head_baseptr->num_entries;
	}while(n!=pos);
	
	printf("KVS %i: DID NOT FIND\n", mpi_world_rank);

	//Error, did not find process set
	KVS_intern_unlock();
	
	exit(-1);
}

void KVS_Put_internal(char *key, int num_ranks, int *ranks, bool lock){
	
	if(lock) KVS_intern_lock();
	
	int pos;
	if(0 > (pos = locate_set(key))){
		printf("KVS %i: Put_initial, did not find set\n", mpi_world_rank);
		exit(-1); 
	}
	
	update_memory(pos);
	
	//Do we change memory size?
	if(num_ranks > entries_baseptr[pos].mem_ranks){
		rescale_memory_ranks(pos, entries_baseptr[pos].mem_ranks, num_ranks);
	}
	
	head_baseptr->version++;
	entries_baseptr[pos].version++;
	entries_baseptr[pos].num_ranks = num_ranks;

	for(int i = 0; i < num_ranks; i++){
		ranks_baseptr[pos][i] = ranks[i];
	}
	head_baseptr->version++;
	
	//Send out notifications that the set changed
	int num = KVS_VERSION_UPDATE;
	for(int i = 0; i < entries_baseptr[pos].num_updates; i++){
		MPI_Send(&num, 1, MPI_INT, updates_baseptr[pos][i], pos, MPI_COMM_WORLD);
	}
	entries_baseptr[pos].num_updates = 0;
	
	if(lock) KVS_intern_unlock();
}

void KVS_Put(char *key, int num_ranks, int *ranks){
	KVS_Put_internal(key, num_ranks, ranks, true);
}

//fetches the value of a process set from KVS (user must free memory at ranks)
void KVS_Get_internal(char *key, int *num_ranks, int **ranks, int *version, int *setnumber, bool lock){
	//For mpi://SELF
	if(strcmp(key, "mpi://SELF") == 0){
		*num_ranks = 1;
		*version = 1;
		*ranks = (int*)malloc(*num_ranks * sizeof(int) + 1);
		*ranks[0] = mpi_world_rank;
		*setnumber = -1; //TODO: Should this even have a setnumber?

		return;
	}
	
	if(lock) KVS_intern_lock();
	
	int pos;
	if(0 > (pos = locate_set(key))){
		printf("KVS %i: Get_internal, did not find set\n", mpi_world_rank);
		exit(-1); 
	}
	
	update_memory(pos);
	
	*num_ranks = entries_baseptr[pos].num_ranks;
	*version = entries_baseptr[pos].version;
	*ranks = (int*)malloc(*num_ranks * sizeof(int));
	for(int i = 0; i < *num_ranks; i++){
		(*ranks)[i] = ranks_baseptr[pos][i];
	}
	*setnumber = pos;
	
	if(lock) KVS_intern_unlock();
}

void KVS_Get(char *key, int *num_ranks, int **ranks, int *version, int *setnumber){
	KVS_Get_internal(key, num_ranks, ranks, version, setnumber, true);
}

//I cannot lock outside, so I move this inside
void KVS_Add(char *key, int rank){
	KVS_intern_lock();
	
	int num_ranks, version, *ranks, setnumber;
	KVS_Get_internal(key, &num_ranks, &ranks, &version, &setnumber, false);
		
	int *nranks = malloc(sizeof(int) * (num_ranks + 1));
	for(int i = 0; i < num_ranks; i++)
		nranks[i] = ranks[i];
	
	ranks[num_ranks] = rank;
	num_ranks++;
	
	KVS_Put_internal(key, num_ranks, ranks, false);
	
	free(ranks);
	free(nranks);
	
	KVS_intern_unlock();
}

void KVS_Del(char *key, int rank){
	KVS_intern_lock();
	
	int num_ranks, version, *ranks, setnumber;
	KVS_Get_internal(key, &num_ranks, &ranks, &version, &setnumber, false);
	for(int i = 0, j = 0; i < num_ranks; i++){
		if(i != rank){
			ranks[j] = ranks[i];
			j++;
		}
	}
	num_ranks--;
	KVS_Put_internal(key, num_ranks, ranks, false);

	free(ranks);
	
	KVS_intern_unlock();
}

//sets up shared memory stores process set information into the KVS
//Only called by one process, others call KVS_open
void KVS_initialise(){

	//Setup shared memory and semaphore
	allocate_KVS_head();
	head_baseptr->num_entries = mpi_nsets+1;
	head_baseptr->version = 0;
	KVS_intern_create_lock();

	allocate_KVS_entries();
	allocate_ranks_and_updates();
	
	//Add world process set
	int *ranks = malloc(sizeof(int) * mpi_world_size);
	for(int i = 0; i < mpi_world_size; i++)
		ranks[i] = i;
	KVS_Put_initial("mpi://WORLD", mpi_world_size, ranks);
	free(ranks);

	//Add other process sets
	for(int i=0; i<head_baseptr->num_entries-1; i++){
		char *setname = malloc(strlen(mpi_setnames[i])+10);
		strcpy(setname,"app://");
		strcat(setname, mpi_setnames[i]);
		
		int localrank=0;
		int num_ranks = mpi_set_upper[i] - mpi_set_lower[i] + 1;
		int *ranks = malloc(sizeof(int) * num_ranks);
		for(int j=0;j<mpi_world_size;j++){
			if(j>=mpi_set_lower[i]&&j<=mpi_set_upper[i]){
				ranks[localrank] = j;
				localrank++;
			}
		}
		
		KVS_Put_initial(setname, num_ranks, ranks);
		
		//no updates in the beginning so no initialization
		
		free(setname);
		free(ranks);
	}
	
	//Local memory information to detect if another process resized a shared 
	//memory block and then resize yourself
	mem_version = malloc(sizeof(int)*head_baseptr->num_entries);
	mem_ranks = malloc(sizeof(int)*head_baseptr->num_entries);
	mem_updates = malloc(sizeof(int)*head_baseptr->num_entries);
	for(int i = 0; i < head_baseptr->num_entries; i++)
		update_local_memory_info(i);
}

//get acces to existing KVS
void KVS_open(){
	open_KVS_head();
	open_KVS_entries();
	open_ranks_and_updates();
	
	mem_version = malloc(sizeof(int)*head_baseptr->num_entries);
	mem_ranks = malloc(sizeof(int)*head_baseptr->num_entries);
	mem_updates = malloc(sizeof(int)*head_baseptr->num_entries);
	for(int i = 0; i < head_baseptr->num_entries; i++)
		update_local_memory_info(i);
}

void KVS_free()
{
	deallocate_ranks_and_updates();
	deallocate_KVS_entries();
	deallocate_KVS_head();
	
	KVS_intern_destroy_lock();
	
	free(mem_version);
	free(mem_ranks);
	free(mem_updates);
}

//Get number of existing process sets(including own mpi://SELF)
int KVS_Get_global_nsets(){
	KVS_intern_lock();
	int ret = head_baseptr->num_entries;
	KVS_intern_unlock();
	return ret+1; //mpi://SELF included
}

//Get number of process sets this process is part of
int KVS_Get_local_nsets(){
	int global_nsets = KVS_Get_global_nsets();
	int count = 0;
	//Check all sets, saved in the KVS
	for(int i=0; i<global_nsets-1; i++){
		if(MPI_Session_check_in_processet(entries_baseptr[i].key)){
			count++;
		}
	}
	//for mpi://SELF
	count++;

	return count;
}

//fetches the names of all process sets(including own mpi://SELF)
//returned pointer must be freed by the user
char** KVS_Get_global_processsets(int n){
	KVS_intern_lock();
	
	char **gps_names = (char**) malloc(sizeof(char*)*n);
	for(int i=0;i<n-1;i++){ //TODO: HACKY, check whether we really need global mpi://SELFi 
		gps_names[i] = (char*) malloc(sizeof(char) * entries_baseptr[i].key_length + 1);
		strcpy(gps_names[i], entries_baseptr[i].key);
	}
	//TODO: For now only own mpi://SELF
	const char s[] = "mpi://SELF";
	gps_names[n-1] = (char*) malloc(sizeof(char) * (strlen(s)+1));
	strcpy(gps_names[n-1], s);
	
	KVS_intern_unlock();
	return gps_names;
}

//fetches the names of process sets this process is part of
//returned pointer must be freed by the user
char** KVS_Get_local_processsets(int n){
	int gnsets = KVS_Get_global_nsets();
	char **gps_names = KVS_Get_global_processsets(gnsets);
	char **lps_names = (char**) malloc(sizeof(char*)*n);
	
	int pos = 0;
	for(int i=0; i<gnsets; i++){
		if(MPI_Session_check_in_processet(gps_names[i])){
			lps_names[pos] = (char *) malloc(sizeof(char) * strlen(gps_names[i]) + 1);
			strcpy(lps_names[pos], gps_names[i]);
			free(gps_names[i]);
			pos++;
		}
	}
	free(gps_names);
	
	return lps_names;
}

//add newly spawned processes to mpi://WORLD, if needed
void KVS_addto_world(){
	KVS_intern_lock();

	int num_ranks, version, *ranks, setnumber;
	KVS_Get_internal("mpi://WORLD", &num_ranks, &ranks, &version, &setnumber, false);

	int position = num_ranks;
	num_ranks = num_ranks + mpi_world_size;
	int *nranks = malloc(sizeof(int) * num_ranks);
	
	for(int i = 0; i < position; i++){
		nranks[i] = ranks[i];
	}
	
	for(int i = 0; i < mpi_world_size; i++){
		nranks[i] = position;
		position++;
	}
	
	KVS_Put_internal("mpi://WORLD", num_ranks, nranks, false);
	
	free(ranks);
	free(nranks);
	
	KVS_intern_unlock();
}

void KVS_ask_for_update(int setnumber){
	KVS_intern_lock();

	int num_updates = entries_baseptr[setnumber].num_updates;
	if(num_updates >= entries_baseptr[setnumber].mem_updates)
		rescale_memory_updates(setnumber, entries_baseptr[setnumber].mem_updates, num_updates+1);
		
	updates_baseptr[setnumber][num_updates] = mpi_world_rank;
	entries_baseptr[setnumber].num_updates++;

	KVS_intern_unlock();
}

//issues a watch on the process set; newly spawned thread is calling this routine
//deprecated ?!
void *KVS_Watch_keyupdate(void *set_void_info){
	/*
	//All this is from before me, no real idea if I really need it
	MPI_Info *s_info = (MPI_Info *)set_void_info;

	char number_str[10], name[50], version_str[10];
	int info_flag, setnumber, version;

	MPI_Info_get(*s_info, "setnumber", 10, number_str, &info_flag);
	MPI_Info_get(*s_info, "setname", 50, name, &info_flag);
	MPI_Info_get(*s_info, "version", 10, version_str, &info_flag);
	setnumber = strtol(number_str, NULL, 10);
	version = strtol(version_str, NULL, 10);

	//Wait till someone sends a notification
	printf("KVS %i: wait for change in %i\n", mpi_world_rank, setnumber);
	int buff; 
	MPI_Irecv(&buff, 1, MPI_INT, MPI_ANY_SOURCE, setnumber, MPI_COMM_WORLD, requests + setnumber);
	//Check if this was completed in the other function
	*/
	printf("KVS %i: THIS ROUTINE SHOULD NOT BE NECESSARY ANYMORE! WHY IS IT CALLED?\n", mpi_world_rank);
	exit(-47);
	return NULL;
}

//issues a watch on the process set
int KVS_Watch_keyupdate_blocking(char *set_name){
	//Fuck it, let's do polling for now: 
	//Maybe I'll find a better version later
	/*
	int num_ranks, version, *ranks, setnumber;
	KVS_Get(set_name, &num_ranks, &ranks, &version, &setnumber);

	//Wait till someone sends a notification
	int buff;
	MPI_Recv(&buff, 1, MPI_INT, MPI_ANY_SOURCE, setnumber, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

	free(ranks);*/
	printf("KVS %i: THIS ROUTINE SHOULD NOT BE NECESSARY ANYMORE! WHY IS IT CALLED?\n", mpi_world_rank);
	return 1;
}

///returns the latest version of the key-value store (built-in version number)
int KVS_Get_kvsversion(){
	int version; 
	KVS_intern_lock();
	version = head_baseptr->version;
	KVS_intern_unlock();
	return version;
}



