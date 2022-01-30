#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
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

//Messages to send to client
#define WHO "WHO:\n"
#define NAME_TAKEN "NAME_TAKEN:\n"
#define KICK "KICK:\n"
#define ENTER "ENTER"
#define LEAVE "LEAVE"
#define MSG "MSG"
#define AUTH "AUTH:\n"
#define OK "OK:\n"

//Messages to receive from client
#define NAME "NAME"
#define CAUTH "AUTH"
#define SAY "SAY"
#define CKICK "KICK"
#define CLEAVE "LEAVE"
#define LIST "LIST"

//Communciations error return code
#define COMMSERR 2

//Number of chat statistics for client and server
#define NUM_CLI_STATS 3
#define NUM_SVR_STATS 6
#define CLIENT_DELAY 100000

//Info needed to communicate with client
struct ClientInf {
    char* name;
    FILE* writeSock;
    FILE* readSock;
    int* clientStats;
    pthread_t threadId;
    struct ClientInf* next;
};

//Parameters needed for child thread 
//to communicate with the client
struct ThreadInf {
    struct ClientInf** head;
    int fd;
    char* auth;
    int* serverStats;
    sem_t* clientsLock;
};

/*
* Given a port number, attempt to connect to that port on localhost and save
* all information needed for future communications. Heavily inspired by lecture
* example mutlithreadingserver.c.
*
* Parameters:
*     port: string representation of the port number. If "0", assign ephemeral
*     port
*     serverfd: a pointer to the file descriptor that the server will use to
*     accept connections in the future
*     portNum: a pointer to the unsigned integer representation of the port
*     number that the server has connected to.
*
* Returns:
*     The error code of this function. 0 is all good, 2 is communications error
*/
int init_comms(const char* port, int* serverfd, unsigned int* portNum) {

    struct addrinfo* ai = 0;
    struct addrinfo hints;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_socktype = AI_PASSIVE;

    int err;
    if ((err = getaddrinfo("localhost", port, &hints, &ai))) {
        return COMMSERR;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    
    if (strcmp(port, "0")) {
        
        int optVal = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optVal, 
                sizeof(int)) < 0) {
            return COMMSERR;
        }
    }

    struct sockaddr* socketAddr = (struct sockaddr*) ai->ai_addr;
    if (bind(fd, socketAddr, sizeof(struct sockaddr)) < 0) {
        return COMMSERR;     
    }
  
    //Get port number to print out
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    socklen_t length = sizeof(struct sockaddr_in);
    getsockname(fd, (struct sockaddr*) &addr, &length);
    
    if (listen(fd, SOMAXCONN) < 0) {
        return COMMSERR;
    }
    
    *portNum = ntohs(addr.sin_port);
    *serverfd = fd;
        
    return 0;
}

/*
* Given two strings, determines if they appear in lexographical order as given 
* in the parameter list. Taken from my assignment 1 solution where it was 
* called compare_words. 
*
* Parameters:
*     word1: the first word to compare
*     word2: the second word to compare
*
* Returns:
*     True if word1 is first lexographically or if words are equal. False
*     otherwise.
*/
bool are_ordered(char* word1, char* word2) {
    
    int length1 = strlen(word1);
    int length2 = strlen(word2);

    int shortestLength = length1;
    bool result = true;

    if (length2 < shortestLength) {
        shortestLength = length2;
        result = false;
    }

    for (int index = 0; index < shortestLength; index++) {
        if (word1[index] < word2[index]) {
            result = true;
            break;
        } else if (word1[index] > word2[index]) {
            result = false;
            break;
        }
    }
    return result;
}

/*
* Free the resources associated with a particular client after it has been
* disconnected.
*
* Parameters:
*     client: the client whose resources the function will free
*/
void free_client(struct ClientInf* client) {
    
    free(client->name);
    fclose(client->writeSock);
    fclose(client->readSock);
    free(client->clientStats);

    free(client);
}

/*
* Remove a client from the linked list structure we have defined to store the
* client list.
*
* Parameters:
*     head: a reference to the head of the list
*     name: the name of the client to remove from the list. names must be
*     unique among clients, so this is a reliable way to remove the correct
*     client
*/
void delete_client(struct ClientInf** head, char* name) {
    
    struct ClientInf* current = *head;
    struct ClientInf* previous = NULL;

    while (current != NULL) {
        
        if (!strcmp(current->name, name)) {
            
            if (previous == NULL && current->next == NULL) {
                //Element is the only one in list
                *head = NULL;
            } else if (previous == NULL) {
                *head = current->next;
            } else {
                previous->next = current->next;
            }

            free(current);
            break;
        }
        
        previous = current;
        current = current->next;
    }
     
}

/*
* Given information relating to a client, create the new client and then insert
* the client into linked list structure in lexographical order of names.
*
* Parameters:
*     head: a reference to the first element in the structure
*     name: the name of the client to insert into the list
*     writeSock: the file pointer used to write to this client
*     readSock: the file pointer used to read from this client
*
* Returns:
*     The client that has been added to the list.
*/
struct ClientInf* insert_client(struct ClientInf** head, char* name, 
        FILE* writeSock, FILE* readSock) {
     
    struct ClientInf* newClient = malloc(sizeof(struct ClientInf));
    newClient->name = malloc(strlen(name) + 1);
    strcpy(newClient->name, name);
    newClient->writeSock = writeSock;
    newClient->readSock = readSock;
    newClient->clientStats = malloc(sizeof(int) * 3);
    newClient->threadId = pthread_self();
    
    for (int index = 0; index < 3; index++) {
        newClient->clientStats[index] = 0;
    }

    //Pointer to head and previous nodes
    struct ClientInf* currentNode = *head;
    struct ClientInf* previousNode = NULL;
    newClient->next = NULL;

    if (currentNode == NULL) {
        *head = newClient;
        return newClient;
    }

    while (true) {
        
        //If new name comes before current node alphabetically
        if (are_ordered(newClient->name, currentNode->name)) {
            newClient->next = currentNode;
            
            //Is no previous client, must be head node
            if (previousNode == NULL) {
                *head = newClient;
            } else {
                previousNode->next = newClient;
            }
            break;
        } else {
            //Update current node
            previousNode = currentNode;
            currentNode = currentNode->next;

            if (currentNode == NULL) {
                previousNode->next = newClient;
                break;
            }
        }
    }
    return newClient;
}

/*
* Given the information relating to a potential client (not yet connected),
* request a name from that client and check if it is taken. Add client if not
* taken.
*
* Parameters:
*     head: a reference to the first element in the list of connected clients
*     writeSock: the file pointer needed to write to this potential client
*     readSock: the file pointer needed to read from this potential client
*     clientsLock: the lock needed to safely access the linked list structure
*
* Returns:
*     NULL if name given by client is taken, client object if negotiation was
*     successful.
*/
struct ClientInf* negotiate_name(struct ClientInf** head, 
        FILE* writeSock, FILE* readSock, sem_t* clientsLock, bool* invalid) {
    
    //Before using any sockets, take the lock
    take_lock(clientsLock);
    write_socket(writeSock, WHO);

    char* message = read_socket(readSock);    
    char** terms = malloc(sizeof(char**));
    int numTerms = unpack_query(terms, message);
     
    if (numTerms != 2) {
        *invalid = true;
        return NULL;
    }

    struct ClientInf* current = *head;

    while (current != NULL) {
        
        //Name taken
        if (!strcmp(current->name, terms[1])) {
            write_socket(writeSock, NAME_TAKEN);
            return NULL;
        }
        
        current = current->next;
    }

    struct ClientInf* res = insert_client(head, terms[1], writeSock, readSock);
    release_lock(clientsLock);
    fprintf(stdout, "(%s has entered the chat)\n", terms[1]);
    fflush(stdout);
    return res;
}

/*
* Given information relating to a potential client, request authentication from
* that client.
*
* Parameters:
*     writeSock: the file pointer needed to write to this potential client
*     readSock: the file pointer needed to read from this potential client
*     auth: the authentication string to connect to this server
*
* Returns:
*     Whether this client has provided the correct authentication string.
*/
bool authenticate(FILE* writeSock, FILE* readSock, char* auth) {

    write_socket(writeSock, AUTH); 

    char* line = read_socket(readSock);

    char** terms = malloc(sizeof(char**));
    int numTerms = unpack_query(terms, line);
     
    if (numTerms != 2 || strcmp(auth, terms[1])) {
        free(line);
        return false;
    }
    
    return true;
}

/*
* Called in response to the LIST: command from a connected client. Will list
* out the names of all currently participating clients and send them to the
* requesting client.
*
* Parameters:
*     head: a reference to the first element in list structure
*     client: the client who requested the list of participants
*/
void list_names(struct ClientInf** head, struct ClientInf* client) {
    
    struct ClientInf* current = *head;

    char* message = malloc(strlen(current->name) + 1);
    strcpy(message, current->name);
    
    current = current->next;

    while (current != NULL) {
        
        message = realloc(message, 
                strlen(message) + strlen(current->name) + 2);
        sprintf(message, "%s,%s", message, current->name);
    

        current = current->next;
    } 
    
    char* msgTerms[] = {"LIST", message};
    char* msg = construct_message(msgTerms, 2);    
    write_socket(client->writeSock, msg);
    free(msg);
}

/*
* Function to send out a message to every participating client in the chat.
* This does not include those who have not passed authentication and name
* negotiation.
*
* Parameters:
*     head: a reference to the first element of the list structure
*     message: the message to broadcast to all participating clients
*/
void broadcast_message(struct ClientInf** head, char* message) {
    
    struct ClientInf* current = *head;

    while (current != NULL) {
        write_socket(current->writeSock, message);
        current = current->next;  
    } 
}

/*
* Called in response to a KICK:clientname request from a participating client.
* Will search the list structure and attempt to kicked the named client. If
* client is found, will broadcast appropriate message to the chat participants.
* If no client found, request is silently resolved.
*
* Parameters:
*     head: a reference to the first element in the list structure
*     name: the name of the client to attempt to kick
*/
void attempt_kick(struct ClientInf** head, char* name, 
        struct ClientInf* kicker) {
    
    struct ClientInf* current = *head; 

    char* msgTerms[] = {LEAVE, name};
    char* msg = construct_message(msgTerms, 2);
    
    if (!strcmp(kicker->name, name)) {
        write_socket(kicker->writeSock, KICK);
        delete_client(head, kicker->name);
        broadcast_message(head, msg);
        fprintf(stdout, "(%s has left the chat)\n", name);
        fflush(stdout);
        pthread_exit((void*) 3);  
    }

    while (current != NULL) {
        
        if (!strcmp(name, current->name)) {
            write_socket(current->writeSock, KICK);
            pthread_cancel(current->threadId);
            delete_client(head, current->name);
            broadcast_message(head, msg); 
            fprintf(stdout, "(%s has left the chat)\n", name);
            fflush(stdout);
        }
        current = current->next;
    }

    free(msg);
}

/*
* Given a message from a client, process the message and perform the
* appropriate actions. Invalid messages are silently ignored
*
* Parameters:
*     head: a reference to the first element in the list structure
*     client: the client from which this message was received
*     lock: the lock needed to safely access the list structure
*     line: the line received from client for processing
*
* Returns:
*     whether this message indicates that this client is finished talking and
*     is about to disconnect.
*/
bool process_message(struct ClientInf** head, struct ClientInf* client, 
        sem_t* lock, int* serverStats, char* line) {

    bool isDone = false;

    char** terms = malloc(sizeof(char**));
    int numTerms = unpack_query(terms, line);

    if (numTerms == 2 && !strcmp(SAY, terms[0])) {
        client->clientStats[0] += 1;
        serverStats[2] += 1;
        char* msgTerms[] = {MSG, client->name, terms[1]};
        char* msg = construct_message(msgTerms, 3);
        broadcast_message(head, msg);
        fprintf(stdout, "%s: %s\n", client->name, terms[1]);
        fflush(stdout);
        free(msg);

    } else if (numTerms == 2 && !strcmp(CKICK, terms[0])) {
        client->clientStats[1] += 1;
        serverStats[3] += 1;

        attempt_kick(head, terms[1], client);

    } else if (numTerms == 1 && !strcmp(CLEAVE, terms[0])) {
        serverStats[5] += 1;
        fprintf(stdout, "(%s has left the chat)\n", client->name);
        fflush(stdout);
        delete_client(head, client->name);
        isDone = true;

    } else if (numTerms == 1 && !strcmp(LIST, terms[0])) {
        client->clientStats[2] += 1;
        serverStats[4] += 1;  
        list_names(head, client);
    }

    return isDone;
}   

/*
* Given the information relating to a client, enter a loop of sending and 
* receiving messages from client and performing the proper processing on those
* messages. Loop continues until client is kicked or has left.
*
* Parameters:
*     head: a reference to the first element of the list structure
*     client: the client that this function is communicating with
*     lock: the lock needed to safely access the list structurw
*/
void talk(struct ClientInf** head, struct ClientInf* client, sem_t* lock, 
        int* serverStats) {
    
    while (true) {
        
        char* line = read_socket(client->readSock); 

        if (!strcmp("EOF", line)) { 
            char* msgTerms[] = {LEAVE, client->name};
            char* msg = construct_message(msgTerms, 2);
            fprintf(stdout, "(%s has left the chat)\n", client->name);
            fflush(stdout);
            delete_client(head, client->name);
            broadcast_message(head, msg);
            free(msg);
            pthread_exit((void*) 2);
        }

        take_lock(lock);

        if (process_message(head, client, lock, serverStats, line)) {
            release_lock(lock);
            break;
        }

        release_lock(lock);
        free(line);
        usleep(CLIENT_DELAY);
    }
}

/*
* The thread function that begins the communciations with a client once they
* have been officially connected. Encapsulates the entire conversation between
* the server and a particular client.
*
* Parameters:
*     arg: compulsary void* arg to thread function. Actually contains all
*     information needed to communicate with a particular client
*
* Returns:
*     compulsary void* return value. Is actually an integer value indicating
*     whether an error has occured or if the thread has ended naturally.
*/
void* client_thread(void* arg) {
    
    struct ThreadInf threadInf = *(struct ThreadInf*) arg;
    struct ClientInf** head = threadInf.head;
    char* auth = threadInf.auth;
    int fd = threadInf.fd;    
    int fd2 = dup(fd);
    int* serverStats = threadInf.serverStats;
    sem_t* clientsLock = threadInf.clientsLock; 
       
    FILE* writeSock = fdopen(fd, "w");
    FILE* readSock = fdopen(fd2, "r"); 
    
    serverStats[0] += 1;
    if (!authenticate(writeSock, readSock, auth)) {
        fclose(writeSock);
        fclose(readSock);
        pthread_exit((void*)(2));
    }

    take_lock(clientsLock);
    write_socket(writeSock, OK);
    release_lock(clientsLock);
    
    struct ClientInf* client; 
    bool invalid = false;

    serverStats[1] += 1;
    //While name hasn't been negotiated
    while ((client = negotiate_name(head, writeSock, readSock, 
                clientsLock, &invalid)) == NULL) { 
        if (invalid) {
            fclose(writeSock);
            fclose(readSock);
            pthread_exit(0);
        }

        release_lock(clientsLock);
        serverStats[1] += 1;
    }
    
    take_lock(clientsLock);
    write_socket(writeSock, OK);
    char* msgTerms[] = {ENTER, client->name};
    char* msg = construct_message(msgTerms, 2);
    broadcast_message(head, msg);
    release_lock(clientsLock);

    talk(head, client, clientsLock, serverStats); 
    return (void*) 0;
}

/*
* Function to print the current chat statistics when prompted.
*
* Parameters:
*     head: a reference to the first element in the list structure
*     serverStats: the statistics collected by the server at this point
*     in the chat
*/
void print_stats(struct ClientInf** head, int* serverStats) {
    
    fprintf(stderr, "@CLIENTS@\n");

    struct ClientInf* current = *head;

    while (current != NULL) {
        
        int say = current->clientStats[0];
        int kick = current->clientStats[1];
        int list = current->clientStats[2];
        
        fprintf(stderr, "%s:SAY:%d:KICK:%d:LIST:%d\n", 
                current->name, say, kick, list);
        
        current = current->next;
    }
    
    int auth = serverStats[0];
    int name = serverStats[1];
    int say = serverStats[2];
    int kick = serverStats[3];
    int list = serverStats[4];
    int leave = serverStats[5];

    fprintf(stderr, "@SERVER@\n");
    fprintf(stderr, "server:AUTH:%d:NAME:%d:SAY:%d:KICK:%d:"
            "LIST:%d:LEAVE:%d\n", auth, name, say, kick, list, leave);
    fflush(stderr);

}

/*
* Thread function for the thread that handles the SIGHUP signal. When the
* SIGHUP signal is received, will print out the chat statistics in the 
* required format
*
* Parameters:
*     arg: compulsary void* argument to thread function. Is actually an
*     instance of the ThreadInf struct containing all the statistics needed.
*
* Returns:
*     compulsary void* return value. Returns 0.
*/
void* signal_thread(void* arg) {
    
    struct ThreadInf threadInf = *(struct ThreadInf*) arg;
    struct ClientInf** head = threadInf.head;
    sem_t* clientsLock = threadInf.clientsLock;
    int* serverStats = threadInf.serverStats;

    int signal;
    sigset_t set;

    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    while (true) {
        sigwait(&set, &signal);
        take_lock(clientsLock);
        print_stats(head, serverStats);
        release_lock(clientsLock);
    }

    return (void*) 0;
}

/*
* Initialise the signal mask for all threads.
*/
void init_mask() {
    
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGHUP);
    pthread_sigmask(SIG_BLOCK, &mask, NULL);
}

/*
* Function to initialise the ThreadInf instance needed in the signal_thread
* function and then to create the new thread.
*
* Parameters:
*     head: a reference to the first element in the list structure
*     serverStats: the chat statistics collected by the server at any point
*     in time
*     lock: the lock needed to access the list structure safely
*/
void init_signal_thread(struct ClientInf** head, int* serverStats, 
        sem_t* lock) {
    
    struct ThreadInf* threadInf = malloc(sizeof(struct ThreadInf));
    threadInf->serverStats = serverStats;
    threadInf->head = head;
    threadInf->clientsLock = lock;

    //Signal handling thread
    pthread_t threadId;
    pthread_create(&threadId, NULL, signal_thread, threadInf);
}

/*
* Sit in a loop and accept connections from clients, spawning child threads to
* deal with connection requests from each client. Inspired heavily by lecture
* example multithreadingserver.c.
*
* Parameters:
*     serverfd: the file descriptor to communicate with the server on.
*     auth: the authentication string that clients must provide in order to
*     connect.
*/
void process_connections(int serverfd, char* auth) {
    
    //Need to maintain a linked-list structure of clients with locking
    struct ClientInf* head = NULL;
    sem_t clientsLock;
    init_lock(&clientsLock);

    struct sockaddr_in fromAddr;
    socklen_t fromAddrSize;
    int fd;

    //Block SIGHUP in all threads
    init_mask();

    int serverStats[] = {0, 0, 0, 0, 0, 0};

    //Spawn signal handler thread
    init_signal_thread(&head, serverStats, &clientsLock);

    while (true) {
        fromAddrSize = sizeof(struct sockaddr_in);
    
        //Will wait until connection becomes available
        fd = accept(serverfd, (struct sockaddr*)&fromAddr, &fromAddrSize);

        //Error connecting to client
        if (fd < 0) {
            continue;
        }    

        //Have now successfully connected.
        struct ThreadInf* threadInfo = malloc(sizeof(struct ThreadInf));
        threadInfo->fd = fd;
        threadInfo->head = &head;
        threadInfo->auth = auth;
        threadInfo->clientsLock = &clientsLock;
        threadInfo->serverStats = serverStats;

        pthread_t threadId;
        pthread_create(&threadId, NULL, client_thread, threadInfo); 
    }
}

/*
* Opens auth file and performs basic error checking on command line arguments.
* Initialises connection to the given port number and then calls
* process_connections to start up the conversation.
*/
int main(int argc, char** argv) {
    
    int fd;
  
    if (argc < 2 || argc > 3 || (fd = open(argv[1], O_RDONLY)) == -1) {
        fprintf(stderr, "Usage: server authfile [port]\n");
        fflush(stderr);
        exit(1);
    }

    FILE* authFile = fdopen(fd, "r");
    char* auth = read_input(authFile, true);

    int serverfd = 0;
    char* port;
    unsigned int portNum = 0;
    
    if (argc == 3) {
        port = argv[2];
    } else {
        port = "0";
    }
    
    if (init_comms(port, &serverfd, &portNum) == 2) {
        fprintf(stderr, "Communications error\n");
        return 2;
    }
    
    fprintf(stderr, "%u\n", portNum);
    fflush(stderr);

    process_connections(serverfd, auth);

    return 0;
}