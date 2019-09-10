/*This file is part of the MPI Sessions library.
 * 
 *Author : Vishnu Anilkumar Suma
 *Date   : 15/03/2019
 *
 *This file, mpisessions.h is the headerfile for routines in sessions.c. 
*/

#ifndef MPISESSIONS_H
#define MPISESSIONS_H

#include <mpi.h>
#include <stdbool.h>


#if defined (__cplusplus)
extern "C" 
{
#endif

extern int mpi_nsets, mpi_local_nsets, mpi_global_nsets, mpi_world_size, mpi_world_rank, mpi_localrank, mpi_setnumber, *mpi_setsizes, *mpi_set_lower, *mpi_set_upper, *mpi_namelengths, *mpi_displs, *mpi_keyupdate_flag;
extern char **mpi_setnames;
extern char *mpi_unique_name, *mpi_totalstring;
extern char *program_identifier;
extern MPI_Group mpi_world_group;
extern MPI_Comm mpi_world_comm;

extern bool *requests_valid;
typedef struct{
  MPI_Group* group;
} MPI_Session;

void MPI_Session_preparation(int,char **);
void MPI_Session_init(MPI_Session**);
void MPI_Session_get_nsets(MPI_Session**, int *);
void MPI_Session_get_global_nsets(MPI_Session**, int *);
void MPI_Session_get_pset_names(MPI_Session**, char***, int);
void MPI_Session_get_global_pset_names(MPI_Session**, char***, int);
void MPI_Group_create_from_session(MPI_Session**, char*,MPI_Group*, MPI_Info);
void MPI_Create_worldgroup_from_ps();
void MPI_Comm_create_from_group(MPI_Group, char *, MPI_Comm*, MPI_Info);
void MPI_Session_compute_setparameters(int,char **);
int MPI_Session_check_in_processet(char *);
int MPI_Session_check_psetupdate(MPI_Info);
void MPI_Session_iwatch_pset(MPI_Info*);
int MPI_Session_watch_pset(char *);
int MPI_Session_fetch_latestversion(char *);
void MPI_Session_addto_pset(char *,int);
void MPI_Session_deletefrom_pset(char *, int);
void MPI_Session_get_set_info(MPI_Session**, char *, MPI_Info*);
void MPI_Session_uniquename();
void MPI_Session_gather_processnames(int,int);
void MPI_Session_finalize(MPI_Session**);
void MPI_Session_free();


#if defined (__cplusplus)	
}
#endif

#endif

