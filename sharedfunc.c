#include <stdio.h>
#include <string.h>
#include "sharedfunc.h"
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <semaphore.h>

//Commands given from server
#define WHO "WHO"
#define NAME_TAKEN "NAME_TAKEN"
#define KICK "KICK"
#define LEFT "LEFT"

//Delimiter used by the server in its messages
#define SERVER_DELIM ":"
       
/*
 * Given an input stream, reads a line up to the newline character and returns
 * it. If an unexpected end of file occurs, return "EOF". Appeared in my 
 * assignment3 submission under the same name.
 *
 * Parameters:
 *     stream: the stream to read input from (e.g. stdin)
 *     eofExpected: whether an end of file is expected. chat\config files 
 *     should have eof at some point.
 *
 * Returns:
 *     The line read from the given stream.
*/
char* read_input(FILE* stream, bool eofExpected) {
        
    char* message = malloc(sizeof(char*));
    char read; 
    int index = 0;
    
    while ((read = fgetc(stream)) != '\n' && read != EOF && !feof(stream)) {  
        message = realloc(message, index + 2); 
        message[index] = read;
        message[index + 1] = '\0';
        index += 1;
    }

    if (!eofExpected && read == EOF) { 
        return "EOF";
    }

    return message;
}

/*
 * Given a list of terms (from unpack_query function, frees all these terms
 * to prevent memory leaks. Appeared in my assignment3 submission under the
 * same name
 *
 * Parameters:
 *     terms: the list of terms to free
 *     numTerms: the number of terms in terms
*/
void free_terms(char** terms, int numTerms) {
    for (int index = 0; index < numTerms; index++) {
        free(terms[index]);
    }
    free(terms);
}

/*
 * Given a ':' seperated line, returns a list of all strings seperated by the
 * delimiter. Appeared in my assignment3 submission under the same name.
 *
 * Parameters:
 *     terms: array of strings to put the result in
 *     text: the string to split at the delimiter
 *
 * Returns:
 *     the amount of terms in the terms array
*/
int unpack_query(char** terms, char* text) {
    
    char* newText = malloc(strlen(text) + 1);
    strcpy(newText, text);
    char* save = newText;
 
    int numParams = 0;
    char* token = strtok_r(newText, SERVER_DELIM, &save);
    
    while (token != NULL) {
        terms = realloc(terms, sizeof(char*) * (numParams + 1));
        terms[numParams] = malloc(sizeof(char) * strlen(token) + 1);
        strcpy(terms[numParams], token);
        token = (char*) strtok_r(NULL, SERVER_DELIM, &save);
        numParams += 1;
    }

    return numParams;
}

/*
 * Given a file descriptor pointing to a chatfile, this function will attempt
 * to read it line by line and return the contents in a char**. Appeared in
 * my assignment3 submission under the same name.
 *
 * Parameters:
 *     fd: the file descriptor to read from
 *     cmdCount: pointer to the number of lines found in the chatfile
 *
 * Returns:
 *     array of strings containing the lines in the chatfile.
*/
char** unpack_chatfile(int fd, int* cmdCount) {
    
    int numCmds = 0;
    char** chatCmds = malloc(1);
    char* line = NULL;
    
    FILE* chatfile = fdopen(fd, "r");

    while(true) {
        
        line = read_input(chatfile, true);
        if (strlen(line) == 0) {
            break;
        }
         
        numCmds += 1;
        chatCmds = realloc(chatCmds, sizeof(char*) * numCmds);
        chatCmds[numCmds - 1] = malloc(strlen(line) + 1);
        strcpy(chatCmds[numCmds - 1], line);
        memset(line, 0, strlen(line));
        free(line);
    }    
    fclose(chatfile);
    *cmdCount = numCmds;
    return chatCmds;
}

/**
* Function to write to socket FILE*. Appeared in my assignment 3
* submission named write_pipe.
*
* Parameters:
*     writeFile: the write end of socket
*     message: the message to write to the socket
*/
void write_socket(FILE* writeFile, char* message) {
    fputs(message, writeFile);
    fflush(writeFile);
}

/*
* Function to read a line from socket FILE*. Appeared in my
* assignment 3 submission named read_pipe.
* 
* Parameters:
*     readFile: the read end of the socket
*
* Returns:
*     the response received over readFile.
*/
char* read_socket(FILE* readFile) {
    fflush(readFile);
    char* result = read_input(readFile, false);
    return result;
}

/*
* Takes a series of "terms" and constructs them into a colon delimited message
* to send off.
*
* e.g. in MSG:client:message, the terms are {"MSG", "client", "message"}
*
* Parameters:
*     terms: the terms to construct into a message
*     numTerms: the number of terms in terms
*
* Returns:
*     the constructed message
*/
char* construct_message(char** terms, int numTerms) {
    
    char* response = malloc(strlen(terms[0]) + 1);
    strcpy(response, terms[0]);

    for (int index = 1; index < numTerms; index++) {
        response = realloc(response,
                strlen(response) + strlen(terms[index]) + 2);
        sprintf(response, "%s:%s", response, terms[index]);
    }
      
    response = realloc(response, strlen(response) + 2);
    sprintf(response, "%s\n", response);

    return response;
}

/*
* Given a lock, initialise that lock so that it can be used later. Taken from
* lecture example race3.c.
*
* Parameters:
*     lock: the lock to initialise
*/
void init_lock(sem_t* lock) {
    sem_init(lock, 0, 1);
}
    
/*
* Given a lock, wait for that lock to become available and then take it to
* perform whatever actions are needed.
*
* Parameters:
*     lock: the lock to wait on becoming available
*/
void take_lock(sem_t* lock) {
    sem_wait(lock);
}
        
/*
* When done with the lock taken from take_lock, release it back so that other
* threads can perform their own actions. Taken from lecture example race3.c
*
* Parameters:
*     lock: the lock to release back to all threads for taking
*/
void release_lock(sem_t* lock) {
    sem_post(lock);
}

