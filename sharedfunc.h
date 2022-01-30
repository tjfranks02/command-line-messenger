#include <stdio.h>
#include <stdbool.h>
#include <semaphore.h>

int unpack_query(char** terms, char* text);
char** unpack_chatfile(int fd, int* cmdCount);
void free_terms(char** terms, int numTerms);
char* read_input(FILE* stream, bool eofExpected);
void print_stack(char** msgStack, int* numLines, FILE* stream);
void write_socket(FILE* socket, char* message);
char* read_socket(FILE* socket);
char* construct_message(char** terms, int numTerms);
void init_lock(sem_t* lock);
void take_lock(sem_t* lock);
void release_lock(sem_t* lock);