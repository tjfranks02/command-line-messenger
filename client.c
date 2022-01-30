#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>
#include <semaphore.h>
#include "sharedfunc.h"

//Possible messages from the server
#define WHO "WHO"
#define NAME_TAKEN "NAME_TAKEN"
#define AUTH "AUTH"
#define OK "OK"
#define KICK "KICK"
#define LIST "LIST"
#define SAY "SAY"
#define ENTER "ENTER"
#define LEAVE "LEAVE"
#define MSG "MSG"

//Info required for thread to read from and respond to server
//This includes name of client and number attached to end of
//name for WHO queries
struct SockComms {
    char* name;
    char* auth;
    int clientNum;
    sem_t* authLock;
    FILE* writeSock;
    FILE* readSock;
};

/*
* Function to initialise the connection to the server. Will create
* communications error if server is not active.
*
* Parameters:
*     port: string representation of the port number to connect to
*     files: list of 2 file pointers to populate. files[0] = read, 
*     file[1] = write
*
* Returns:
*     the error code of the function. 2 if coommunication error occured, 0 if
*     all good.
*/
int init_connection(const char* port, FILE** files) {
    
    //Addressing information and connection hints
    struct addrinfo* ai = NULL;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));

    //Can communicate with IPv4 addresses using TCP
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    //Attempt to connect to specified port on this machine
    if (getaddrinfo("localhost", port, &hints, &ai)) {
        return 2;
    }
    
    //Create socket using IPv4 and TCP
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    
    //Connect to socket
    struct sockaddr* socketAddr = (struct sockaddr*) ai->ai_addr;
    if (connect(fd, socketAddr, sizeof(struct sockaddr))) { 
        return 2;
    }
    
    //Dup fd, one for writing one for reading
    int fd2 = dup(fd);
    FILE* writeSock = fdopen(fd, "w");
    FILE* readSock = fdopen(fd2, "r");

    files[0] = readSock; 
    files[1] = writeSock;
        
    return 0;
}

/*
* Function to choose a name for this client. This name is comprised of a base
* name passed in as command line argument and a trailing digit. Is called in
* response to the WHO: command from the server
*
* Parameters:
*     sockInfo: the information needed to communicate with the server socket
*
* Returns:
*     the name selected to represent this client
*/
char* select_name(struct SockComms sockInfo) {
    
    int num = sockInfo.clientNum;
    char* base = sockInfo.name;
    
    //Space for null and newline
    char* response = malloc(strlen(base) + 1);

    if (num != -1) {
        //Add space for trailing digit
        response = realloc(response, strlen(base) + 2);
        sprintf(response, "%s%d", base, num);
    } else {
        sprintf(response, "%s", base);
    }

    return response;

}

/*
* Function to process input taken from stdin. If input starts with '*', will
* take as literal command, otherwise interpreted as a SAY: message.
*
* Parameters:
*     sockInfo: information required to communicate with the server socket
*     line: line from stdin to process
*/
void process_input(struct SockComms* sockInfo, char* line) {
    
    if (line[0] == '*') {
        memmove(line, line + 1, strlen(line));
        line = realloc(line, strlen(line) + 2);
        sprintf(line, "%s\n", line);

        if (!strcmp("LEAVE:\n", line)) {
            exit(0);
        }

        write_socket(sockInfo->writeSock, line);

    } else {
        char* messageTerms[] = {SAY, line};
        char* msg = construct_message(messageTerms, 2);
        write_socket(sockInfo->writeSock, msg);
        free(msg);
    }
}
    
/*
* Function to read input from stdin.
*
* Parameters:
*     sockInfo: information required to communicate with the server socket
*/
void read_in(struct SockComms* sockInfo) {

    while (true) {
        char* line = read_input(stdin, false);
         
        if (!strcmp(line, "EOF")) {    
            break;
        }

        process_input(sockInfo, line);
        memset(line, '\0', strlen(line));
        free(line);
    }
    
    exit(0);
}

/*
* Thread function that wraps the server_read function.
*
* Parameters:
*     arg: compulsary void* argument. is actually a pointer to an instance of
*     struct SockComms
*
* Returns:
*     0
*/
void* init_read(void* arg) {
    usleep(100000);
    read_in((struct SockComms*) arg);
    return 0;
}

/*
* Given a message from the server, this function will determine whether it is
* a "print message" i.e. one that requires printing to the console. Called by
* process message function below. This function mainly exists to get below 50
* lines in the other process message function
*
* Parameters:
*     sockInfo: the information needed to communicate with server socket
*     terms: the terms of the message e.g. MSG:person:hi has terms 
*     [MSG, person, hi]
*     numTerms: the number of terms
*     okExpected: whether the client was expecting an OK response (in response
*     to an auth or name request)
*/
void process_print_messages(struct SockComms* sockInfo, char** terms, 
        int numTerms, bool* okExpected) {
    
    if (numTerms == 1 && strcmp(terms[0], OK) && (*okExpected)) {
        fprintf(stderr, "Authentication error\n");
        exit(4);
    
    } else if (numTerms == 1 && !strcmp(terms[0], KICK)) {
        fprintf(stderr, "Kicked\n");
        exit(3);
    } else if (numTerms == 2 && !strcmp(terms[0], LIST)) {
        fprintf(stdout, "(current chatters: %s)\n", terms[1]);
            
    } else if (numTerms == 3 && !strcmp(terms[0], MSG)) {
        fprintf(stdout, "%s: %s\n", terms[1], terms[2]);
                        
    } else if (numTerms == 2 && !strcmp(terms[0], ENTER) && !strcmp(terms[1],
            select_name(*sockInfo))) {
        release_lock(sockInfo->authLock);
        fprintf(stdout, "(%s has entered the chat)\n", terms[1]);
                        
    } else if (numTerms == 2 && !strcmp(terms[0], ENTER)) {
        fprintf(stdout, "(%s has entered the chat)\n", terms[1]);
    
    } else if (numTerms == 2 && !strcmp(terms[0], LEAVE)) {
        fprintf(stdout, "(%s has left the chat)\n", terms[1]);
    }
}

/*
* Function to take in a message from server and process it, reacting and 
* responding as necessary
*
* Parameters:
*     message: the message recieved from the server
*     sockInfo: the information needed to communicate with with server socket
*     okExpected: whether the response OK: is expected from the server. If OK
*     is expected and not received, client will disconnect.
*
*
*/
void process_message(char* message, struct SockComms* sockInfo, 
        bool* okExpected, int* okCount) { 
    
    char** terms = malloc(sizeof(char**));
    int numTerms = unpack_query(terms, message);  
   
    if (!strcmp("EOF", message) && !(*okExpected)) {
        fprintf(stderr, "Communications error\n");
        exit(2);

    } else if (numTerms == 1 && !strcmp(terms[0], WHO)) {
        char* name = select_name(*sockInfo);
        char* responseTerms[] = {"NAME", name};
        char* msg = construct_message(responseTerms, 2);
        write_socket(sockInfo->writeSock, msg);
        free(name);
        free(msg);

    } else if (numTerms == 1 && !strcmp(terms[0], NAME_TAKEN)) {
        sockInfo->clientNum += 1;

    } else if (numTerms == 1 && !strcmp(terms[0], AUTH)) {
        char* responseTerms[] = {"AUTH", sockInfo->auth};
        *okExpected = true;
        char* msg = construct_message(responseTerms, 2);
        write_socket(sockInfo->writeSock, msg);
        free(msg);
    
    } else if (numTerms == 1 && !strcmp(terms[0], OK)) {
        *okExpected = false;
        *okCount += 1;
    } else {
        process_print_messages(sockInfo, terms, numTerms, okExpected);
    }
}

/*
* Function to read messages sent from the server before sending them on for 
* processing.
*
* Parameters:
*     sockInfo: the informatino required to communicate with server socket
*/
void server_read(struct SockComms* sockInfo) {
    
    //If client receives auth message, it then expects an OK back
    //If no OK shows up it must have failed auth
    bool okExpected = false;
    int okCount = 0;
 
    while (true) {
        
        char* line = read_input(sockInfo->readSock, false);
        process_message(line, sockInfo, &okExpected, &okCount);
    
        memset(line, '\0', strlen(line));
        free(line);
    }

}

/*
* Attempts to open authfile, does some basic error checking on command line 
* args. Then attempts to establish a conection to the server before 
* continously taking input from server and stdin.
*/
int main(int argc, char** argv) {
    
    int fd;

    if (argc != 4 || (fd = open(argv[2], O_RDONLY)) == -1) {
        fprintf(stderr, "Usage: client name authfile port\n");
        fflush(stderr);
        exit(1);
    }
    
    //Get authentication string
    FILE* authFile = fdopen(fd, "r");    
    char* auth = read_input(authFile, true);

    FILE* files[2];

    if (init_connection(argv[3], files) == 2) {
        fprintf(stderr, "Communications error\n");
        fflush(stderr);
        exit(2);
    } 

    //Create thread to take input from server
    struct SockComms* sockComms = malloc(sizeof(struct SockComms*));
    sockComms->name = malloc(strlen(argv[1]) + 1);
    strcpy(sockComms->name, argv[1]);
    sockComms->auth = malloc(strlen(auth) + 1);
    strcpy(sockComms->auth, auth);
    sockComms->clientNum = -1;
    
    sem_t authLock;
    init_lock(&authLock);
    sockComms->authLock = &authLock;
    
    sockComms->readSock = files[0];
    sockComms->writeSock = files[1];     

    fclose(authFile);
    
    pthread_t threadId;
    pthread_create(&threadId, NULL, init_read, (void*) sockComms);

    server_read(sockComms);

    return 0;
}