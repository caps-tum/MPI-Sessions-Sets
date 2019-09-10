/*This file is part of the MPI Sessions library.
 * 
 *Author : Vishnu Anilkumar Suma
 *Date   : 15/03/2019
 *
 *This file, fluxkvs.h is the headerfile for routines in kvs.c. 
*/


#ifndef FLUXKVS_H
#define FLUXKVS_H


int FLUX_Get_local_nsets();
int FLUX_Get_global_nsets();
char** FLUX_Get_local_processsets(int);
char** FLUX_Get_global_processsets(int);
char* FLUX_Get(char *);
void FLUX_Put(char *, const char *);
void FLUX_Kvs_initialise();
void FLUX_Kvs_addto_world();
void *FLUX_Watch_keyupdate(void *);
int FLUX_Watch_keyupdate_blocking(char *);
int FLUX_Fetch_latestversion(char *);
int FLUX_Get_kvsversion();
void FLUX_Wait_kvsversion(int);
struct json_object* FLUX_Fetch_jsonvalue(struct json_object*,char *);
int count_words(char *);
#endif
