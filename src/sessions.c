/*This file is part of the MPI Sessions library.
 * 
 *Author : Vishnu Anilkumar Suma
 *Date   : 15/03/2019
 *
 *This file, sessions.c implements all the routines included in the Set Management Module. 
 *Internally calls routines of Open MPI. 
 */

#include <stdio.h>
#include <mpisessions.h>
#include <kvs.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>

int mpi_nsets=0, mpi_local_nsets=0, mpi_global_nsets=0, mpi_world_rank, 
    mpi_world_size, mpi_setnumber, *mpi_setsizes=NULL, *mpi_set_lower=NULL, 
    *mpi_set_upper=NULL, *mpi_namelengths=NULL, *mpi_displs=NULL, 
    *mpi_keyupdate_flag=NULL;
char **mpi_setnames=NULL, **mpi_local_process_sets=NULL, 
     **mpi_global_process_sets=NULL; 
char *mpi_unique_name=NULL, *mpi_totalstring=NULL;

char *program_identifier = "/mpisessions"; //Should be set by mpirun to allow multiple programs, used to created shared memory

MPI_Request *requests; 
int buff;

MPI_Group mpi_world_group;
MPI_Comm mpi_world_comm;

void update_request(MPI_Request *req, int setnumber)
{
	if(*req != MPI_REQUEST_NULL){
		MPI_Cancel(req);
		MPI_Request_free(req);
		int buff;
		MPI_Irecv(&buff, 1, MPI_INT, MPI_ANY_SOURCE, setnumber, MPI_COMM_WORLD, req);
	}
}

//The MPI standard routine MPI_Comm_spawn is directed to this 
//routine using #pragma weak. Uses PMPI profiling interface
#pragma weak MPI_Comm_spawn = MPIS_Comm_spawn
int MPIS_Comm_spawn(char *command, char *argv[], int maxprocs, MPI_Info info, 
	int root, MPI_Comm comm, MPI_Comm *intercomm, int array_of_errcodes[]){

	int val = PMPI_Comm_spawn(command, MPI_ARGV_NULL, maxprocs, MPI_INFO_NULL, 
		root, comm, intercomm, NULL);

	debug_print_KVS(false);

	MPI_Comm intracomm;
	//Merge with the existing world group
	MPI_Intercomm_merge(*intercomm, 0, &intracomm);
	MPI_Barrier(*intercomm);

	MPI_Comm_group(intracomm, &mpi_world_group);
	mpi_world_comm = intracomm;
	
	//Update all requests to use new communicator,doing it explictly now, later global array?
	for(int i = 0; i < mpi_nsets; i++)
		update_request(requests + i, i);

	//Make this exit only if spawned processes are done as well
	MPI_Barrier(MPI_COMM_WORLD);
	
	return val;
}


//initialises the library environment and stores process set information in KVS
void MPI_Session_preparation(int argc, char **argv){
	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &mpi_world_rank);
	MPI_Comm_size(MPI_COMM_WORLD, &mpi_world_size);

	MPI_Comm parent;
	MPI_Comm_get_parent(&parent);	
	int flag;
	
	if(parent==MPI_COMM_NULL){
		flag = 0;
	}
	else{
		flag = 1;
	}

	//parent process
	if(!flag){
		MPI_Comm_group(MPI_COMM_WORLD, &mpi_world_group);
		MPI_Session_compute_setparameters(argc,argv);
		MPI_Session_uniquename();
		MPI_Session_gather_processnames(mpi_world_rank,mpi_world_size);
		
		//add processes to mpi://WORLD
		if(mpi_world_rank == 0){
			KVS_initialise();
			MPI_Barrier(MPI_COMM_WORLD);
		}
		else{
			MPI_Barrier(MPI_COMM_WORLD);
			KVS_open(false);
		}
		//usng mpi://WORLD to generate a world group and a world communicator

		MPI_Create_worldgroup_from_ps();
		
		MPI_Group_size(mpi_world_group, &mpi_world_size);
		MPI_Group_rank(mpi_world_group, &mpi_world_rank);
		MPI_Comm_create_group(MPI_COMM_WORLD, mpi_world_group, 0, &mpi_world_comm);
	}
	//child process
	else{
		MPI_Session_uniquename();	

		//MPI_Session_gather_processnames(mpi_world_rank, mpi_world_size);

		KVS_open();
		debug_print_KVS(true);
		
		//Moved creation of shared communicator up
		MPI_Barrier(MPI_COMM_WORLD);

		MPI_Comm parentcomm, intracomm;
		MPI_Comm_get_parent(&parentcomm);
		
		MPI_Intercomm_merge(parentcomm, 1, &intracomm);
		MPI_Barrier(parentcomm);		

		MPI_Comm_group(intracomm,&mpi_world_group);
		MPI_Group_rank(mpi_world_group, &mpi_world_rank);
		mpi_world_comm = intracomm;
		//add spawned processed to mpi://WORLD
		if(mpi_world_rank ==0){
			KVS_addto_world(intracomm);
		}
		
		/* Uncomment this to add the spawned processes to mpi://WORLD */
		//MPI_Session_addto_ps("mpi://WORLD");
		
		
		//Make this exit only if spawning processes are done as well
		MPI_Barrier(MPI_COMM_WORLD);
	}
	
 	mpi_nsets = KVS_Get_global_nsets();
	requests = malloc(sizeof(MPI_Request) * mpi_nsets);
	for(int i = 0; i < mpi_nsets; i++) requests[i] = MPI_REQUEST_NULL;
}

//create a session
void MPI_Session_init(MPI_Session** mpisession){
	*mpisession = malloc(sizeof(MPI_Session));
}

//fetch the local number of process sets
void MPI_Session_get_nsets(MPI_Session** mpisession, int *n){
	if(mpisession == NULL){
		return;
	}

	*n = KVS_Get_local_nsets();
	mpi_local_nsets = *n;
}

//fetch the total number of process sets
void MPI_Session_get_global_nsets(MPI_Session** mpisession, int *n){

	if(mpisession == NULL){
		return;
	}

	*n = KVS_Get_global_nsets();
	mpi_global_nsets = *n;
}

//fetch the names of process sets
void MPI_Session_get_pset_names(MPI_Session** mpisession, char*** names, int n){

	if(mpisession == NULL){
		return;
	}

	mpi_local_process_sets = KVS_Get_local_processsets(n);
	*(names)=mpi_local_process_sets;
}

//fetch the names of all the process sets
void MPI_Session_get_global_pset_names(MPI_Session** mpisession, 
		char*** names, int n){

	if(mpisession == NULL){
		return;
	}

	mpi_global_process_sets = KVS_Get_global_processsets(n);
	
	*(names)=mpi_global_process_sets;
}

//create a world group from the process set mpi://WORLD
void MPI_Create_worldgroup_from_ps(){
	int num_ranks, version, *ranks, setnumber;
	
	KVS_Get("mpi://WORLD", &num_ranks, &ranks, &version, &setnumber);

	MPI_Group new_group;
	MPI_Group_incl(mpi_world_group, num_ranks, ranks, &new_group);

	mpi_world_group = new_group;

	free(ranks);
}

//create a group for a process set
void MPI_Group_create_from_session(MPI_Session** mpisession, char* set_name, 
	MPI_Group* group, MPI_Info set_info){

	if(mpisession == NULL){
		return;
	}
	
	char version_str[10];
	int info_flag, version_from_process;	

	MPI_Info_get(set_info, "version", 10, version_str, &info_flag);	
	version_from_process = strtol(version_str, NULL, 10);
	
	int num_ranks, version, *ranks, setnumber;
	KVS_Get(set_name, &num_ranks, &ranks, &version, &setnumber);
	
	if(version != version_from_process){
		*(group) = MPI_GROUP_NULL;
		return; 
	}
	
	MPI_Group new_group;
	MPI_Group_incl(mpi_world_group, num_ranks, ranks, &new_group);
	*(group) = new_group;

	(*mpisession)->group = &new_group; 

	free(ranks);
}

//create a communicator from a group
void MPI_Comm_create_from_group(MPI_Group group, char *tag, MPI_Comm* comm, 
	MPI_Info set_info){

	if(group == MPI_GROUP_NULL){
		*(comm) = MPI_COMM_NULL;
		return;
	}

	char version_str[10], setname[50];
	int info_flag, version_from_process;

	MPI_Info_get(set_info, "version", 10, version_str, &info_flag);
	version_from_process = strtol(version_str, NULL, 10);
	MPI_Info_get(set_info, "setname", 50, setname, &info_flag);

	int latest_version = MPI_Session_fetch_latestversion(setname);

	if(latest_version != version_from_process){
		printf("danger2 from %d\n",mpi_world_rank);fflush(stdout);
		*(comm) = MPI_COMM_NULL;
		return;
	}
	MPI_Comm new_comm;

	MPI_Comm_create_group(mpi_world_comm, group, 0, &new_comm);
	*(comm) = new_comm;
}

//TODO: Update
//initiate asychronous watch on a process set
void MPI_Session_iwatch_pset(MPI_Info *ps_info){
	char setnumber_str[10], setname[50];
	int info_flag, setnumber;

	MPI_Info_get(*ps_info, "setnumber", 10, setnumber_str, &info_flag);
	MPI_Info_get(*ps_info, "setname", 50, setname, &info_flag);		
	setnumber = strtol(setnumber_str, NULL, 10);
		
	KVS_ask_for_update(setnumber);
	
	//Wait till someone sends a notification
	MPI_Irecv(&buff, 1, MPI_INT, MPI_ANY_SOURCE, setnumber, MPI_COMM_WORLD, requests + setnumber);
	/*
	pthread_t watch_thread;
	pthread_create(&watch_thread, NULL, KVS_Watch_keyupdate, ps_info);*/

	return;
}

//TODO: Update
//initiate a blocking watch on the process set
int MPI_Session_watch_pset(char *set_name){
	int num_ranks, version, *ranks, setnumber;
	KVS_Get(set_name, &num_ranks, &ranks, &version, &setnumber);
	free(ranks);
	
	KVS_ask_for_update(setnumber);
	int buff;
	MPI_Recv(&buff, 1, MPI_INT, MPI_ANY_SOURCE, setnumber, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	return 1;
}

//fetch the latest version number of a process set
int MPI_Session_fetch_latestversion(char *set_name){
	int num_ranks, version, *ranks, setnumber;
	KVS_Get(set_name, &num_ranks, &ranks, &version, &setnumber);
	free(ranks);
	return version;
	//Should I really have an extra function for this?
	//return FLUX_Fetch_latestversion(set_name);
}

//remove the processes from this process set
void MPI_Session_deletefrom_pset(char *set_name, int n){
	KVS_Del(set_name, mpi_world_rank);
}

//add processes to the process set
void MPI_Session_addto_pset(char *set_name, int n){
	KVS_Add(set_name, mpi_world_rank);
}

//read from the file for process set information
void MPI_Session_compute_setparameters(int count, char** values){

	if(mpi_world_rank == 0){
		char filename[25];
		for(int i=0;i<count-1;i++){
			if(strcmp(values[i],"-ps")==0){
				strcpy(filename,values[++i]);
				break;
			}
		}

		FILE *fptr;
		char str[100];
		char *word;

		fptr = fopen(filename,"r");
		if(fptr==NULL){
			printf("Error!,opening file");
			exit(1);
		}

		mpi_nsets = 0;
		while (fgets(str, 100, fptr) != NULL){
			mpi_nsets++;
		}

		mpi_setnames = (char**) malloc (sizeof(char*) * mpi_nsets);
		for (int i=0; i<mpi_nsets; i++) {
			mpi_setnames[i] = (char *) malloc (sizeof(char) * 50);
			if(mpi_setnames[i]==NULL){
				printf("error");
			}
		}
		
		mpi_setsizes = (int *) malloc (sizeof(int) * mpi_nsets);
		mpi_set_lower = (int *) malloc (sizeof(int) * mpi_nsets);
		mpi_set_upper = (int *) malloc (sizeof(int) * mpi_nsets);

		rewind(fptr);
		int i=0;
		while (fgets(str, 100, fptr) != NULL){
			word = strtok (str," \t,.-");
			char *name=NULL,*lowerbound=NULL,*upperbound=NULL;                 

			name = word;
			lowerbound = strtok (NULL, " \t,.-");
			upperbound = strtok (NULL, " \t,.-");
			
			strcpy(mpi_setnames[i],name);
			int lower = strtol(lowerbound, NULL, 10);
			mpi_set_lower[i] = lower;
			int upper = strtol(upperbound, NULL, 10);
			mpi_set_upper[i] = upper;
			mpi_setsizes[i] = upper-lower+1;		
			i++;
			
		}

		fclose(fptr);
	}

	/*
	mpi_keyupdate_flag = (int *) malloc (sizeof(int) * 10);
	for(int i=0;i<10;i++){
		mpi_keyupdate_flag[i] = 0;
	}
	*/
}

//check if the process belongs to the process set
//TODO: I am not using the name, but the ranks here,
//should probably be changed for a real implementation
int MPI_Session_check_in_processet(char *ps_name){

	if(mpi_unique_name==NULL){
		MPI_Session_uniquename();
	}
	
	int num_ranks, version, *ranks, setnumber;
	KVS_Get(ps_name, &num_ranks, &ranks, &version, &setnumber);

	for(int i = 0; i < num_ranks; i++){
		if(mpi_world_rank == ranks[i]){
			free(ranks);
			return 1;
		}
	}
	free(ranks);
	return 0;
}

//check if the issued watch operation on the process set has returned or not
int MPI_Session_check_psetupdate(MPI_Info ps_info){

	char setnumber_str[10];
	int info_flag, setnumber;

	MPI_Info_get(ps_info, "setnumber", 10, setnumber_str, &info_flag);
	setnumber = strtol(setnumber_str, NULL, 10);

	int flag = 0;
	if(requests[setnumber] != MPI_REQUEST_NULL)
		MPI_Test(requests + setnumber, &flag, MPI_STATUS_IGNORE);
	if(flag){
		MPI_Wait(requests + setnumber, MPI_STATUS_IGNORE);
	}
	
	// Request complete
	if(flag)
		requests[setnumber] = MPI_REQUEST_NULL;
	
	/*int flag = mpi_keyupdate_flag[setnumber];
	mpi_keyupdate_flag[setnumber] = 0;*/	
	return flag;
}

//fetch information about the process set from KVS and return an MPI_Info object
void MPI_Session_get_set_info(MPI_Session** mpisession, char *ps_name, 
	MPI_Info *info){

	if(mpisession == NULL){
		return;
	}

	if(strstr(ps_name,"mpi://SELF")){

		MPI_Info info_temp;
		MPI_Info_create(&info_temp);
		MPI_Info_set(info_temp, "size","1");
		MPI_Info_set(info_temp, "version","1");
		MPI_Info_set(info_temp, "setname", ps_name);

		*(info) = info_temp;
		return;
	}

	int num_ranks, version, *ranks, setnumber;
	KVS_Get(ps_name, &num_ranks, &ranks, &version, &setnumber);
	
	//TODO IMPROVEMENt: Should probably change this to be dynamic?
	char size_str[5], version_str[5], setnumber_str[5];
	sprintf(size_str, "%d", num_ranks);
	sprintf(version_str, "%d", version);
	sprintf(setnumber_str,"%d", setnumber); 

	MPI_Info info_temp;
	MPI_Info_create(&info_temp);
	MPI_Info_set(info_temp, "size", size_str);
	MPI_Info_set(info_temp, "version", version_str);
	MPI_Info_set(info_temp, "setnumber", setnumber_str);	
	MPI_Info_set(info_temp, "setname", ps_name);

	*(info) = info_temp;

	free(ranks);
}

//create the unqiue name for the process
void MPI_Session_uniquename(){

	char process[100];
	int name_len;
	long int process_id;

	mpi_unique_name = (char *) malloc (sizeof(char)*(MPI_MAX_PROCESSOR_NAME +100));

	MPI_Get_processor_name(mpi_unique_name, &name_len);
	strcat(mpi_unique_name,"_");

	process_id = getpid();
	sprintf(process, "%ld",process_id);

	strcat(mpi_unique_name, process);
}


void MPI_Session_gather_processnames(int rank, int size){

	int mylen = strlen(mpi_unique_name);

	if(rank == 0){
		mpi_namelengths = malloc( size * sizeof(int));
	}

	MPI_Gather(&mylen, 1, MPI_INT, mpi_namelengths, 1, MPI_INT, 0, MPI_COMM_WORLD);

	int totlen = 0;

	if (rank == 0) {
		mpi_displs = malloc( size * sizeof(int) );
		mpi_displs[0] = 0;
		totlen += mpi_namelengths[0]+1;

		for (int i=1; i<size; i++) {
			totlen += mpi_namelengths[i]+1;
			mpi_displs[i] = mpi_displs[i-1] + mpi_namelengths[i-1] + 1;
		}


		mpi_totalstring = malloc(totlen * sizeof(char));
		for (int i=0; i<totlen-1; i++){
			mpi_totalstring[i] = ' ';
		}
		mpi_totalstring[totlen-1] = '\0';
	}

	MPI_Gatherv(mpi_unique_name, mylen, MPI_CHAR, mpi_totalstring, mpi_namelengths, 
		mpi_displs, MPI_CHAR, 0, MPI_COMM_WORLD);


}

//free the memory utilized by the library
void MPI_Session_free(){
	MPI_Barrier(MPI_COMM_WORLD);
	
	if(mpi_displs != NULL){
		free(mpi_displs);
	}	

	if(mpi_namelengths != NULL){
		free(mpi_namelengths);
	}

	if(mpi_totalstring != NULL){
		free(mpi_totalstring);
	}
	
	if(mpi_setnames != NULL){ 
		// Not counting mpi://SELF + mpi://WORLD, do I have inconsistent use of this var?
		for (int i=0; i<mpi_nsets-2; i++) {
			free(mpi_setnames[i]);
		}
		free(mpi_setnames);
	}

	if(mpi_setsizes != NULL){
		free(mpi_setsizes);
	}

	if(mpi_set_lower != NULL){
		free(mpi_set_lower);
	}

	if(mpi_set_upper != NULL){
		free(mpi_set_upper);
	}

	if(mpi_unique_name != NULL){
		free(mpi_unique_name); 
	}
	
	/*
	if(mpi_keyupdate_flag != NULL){
		free(mpi_keyupdate_flag);
	}
	*/
	
	free(requests);
	
	KVS_free();
	
	
	MPI_Finalize();
}

//free the resource allocated within the session
void MPI_Session_finalize(MPI_Session** session){

	if(mpi_local_nsets > 0){
		for (int i=0; i<mpi_local_nsets; i++) {
			free(mpi_local_process_sets[i]);
		}
		free(mpi_local_process_sets);
	}

	if(mpi_global_nsets > 0){
		if(mpi_global_process_sets != NULL){
		for (int i=0; i<mpi_global_nsets; i++) {
			free(mpi_global_process_sets[i]);
		}
		free(mpi_global_process_sets);
		}
	}

	if(*session != NULL){
		free(*session);
		*session = NULL;
	}
	
}
