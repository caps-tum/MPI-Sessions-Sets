/*This file is part of the MPI Sessions library.
 * 
 *Author : Vishnu Anilkumar Suma
 *Date   : 15/03/2019
 *
 *This file, kvs.h is the headerfile for routines in kvs.c. 
*/


#ifndef KVS_H
#define KVS_H

#include <stdbool.h>
#include <mpi.h>
#include <stdio.h>
#include <stdbool.h>


int KVS_Get_local_nsets();
int KVS_Get_global_nsets();
char** KVS_Get_local_processsets(int);
char** KVS_Get_global_processsets(int);
//void KVS_Get_internal(char *, int*, int**, int*, int*, bool);
//void KVS_Put_internal(char *, int, int*, bool);
void KVS_Get(char *, int*, int**, int*, int*);
void KVS_Put(char *, int, int*);
void KVS_Add(char *, int);
void KVS_Del(char *, int);
void KVS_initialise();
void KVS_open();
void KVS_addto_world();
void *KVS_Watch_keyupdate(void *);
int KVS_Watch_keyupdate_blocking(char *);
//int KVS_Fetch_latestversion(char *);
//int KVS_Get_kvsversion();
void KVS_ask_for_update(int);
void KVS_free();
//int count_words(char *);
//int hash(const char*, int);


void debug_print_KVS(bool);
#endif //KVS_H
