#define _XOPEN_SOURCE 600
#define SO_REUSEPORT 15
#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>

//Global variables that the threads will use
pthread_t threads;
pthread_mutex_t mutex;
pthread_cond_t* conditions;


int* threads_id;
int thread_amount, current_fork, listenfd, port, threads_available;
char* root;

//The request of the client will be stored in these variables


//Function that contorls the threads
void recieve_clients(){
    int slot = 0;
    start_server();
    // Ignore SIGCHLD to avoid zombie threads
    signal(SIGCHLD,SIG_IGN);
    precreate_threads();

    // Accept connections
    while (1){
        printf("Accepting Connections\n");
        threads_id[slot] = 1;

        //Tells the next empty thread to wait for a new client
        pthread_mutex_lock(&mutex);
        pthread_cond_signal(&conditions[slot]);
        pthread_mutex_unlock(&mutex);

        //Waits until a new client is connected to the thread
        pthread_mutex_lock(&mutex);
        pthread_cond_wait(&conditions[thread_amount], &mutex);
        pthread_mutex_unlock(&mutex);

        //When every thread is busy, waits until one of them is free
        if(threads_available == 0){
            out_of_threads();
        }

        //Get a free thread
        slot = check_threads_available();
    }
}

//Start the server (socket)
void start_server() {
    struct sockaddr_in address;

    // Creating socket file descriptor
    if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        printf("error creating control socket : Returning Error No:%d\n", errno);
        return errno;
    }

    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    memset(&(address.sin_zero), 0, 8);

    if(bind(listenfd, (struct sockaddr *)&address, sizeof(struct sockaddr)) == -1){
        printf("Error binding control socket to port %d : Returning error No:%d\n", port, errno);
        return errno;
    }

    if(listen(listenfd, BACKLOG) == -1){
        printf("Error listening at control port : Returning Error No:%d\n", errno);
        return errno;
    }
    printf("\nServer started at port:%d\n", port);

}

//Function tha precreate the threads to be used to receive clients
void precreate_threads(){
    threads_id = (int*)malloc(sizeof(int) * thread_amount);
    conditions = (pthread_cond_t*)malloc(sizeof(pthread_cond_t) * (thread_amount+1));
    threads_available = thread_amount;

    int i = 0;

    while(i < thread_amount){
        pthread_create(&threads, NULL, &attend_client, i);
        pthread_cond_init (&conditions[i], NULL);
        threads_id[i] = 0;
        i = i + 1;
    }
    pthread_cond_init (&conditions[i], NULL);
}

//Function that checks if there is a thread available
//Returns the position in the array if one is found
//If there is no thread available, -1 is returned
int check_threads_available(){
    int answer = -1;
    int i = 0;

    while(answer == -1 && i < thread_amount){
        if(threads_id[i] == 0){
            answer = i;
        }
        i = i + 1;
    }

    return answer;
}

//Thread function, to attend a client
void attend_client(void* arg){
    int i = (int) arg;
    //Waits for the server to tell this thread to accept clients
    pthread_mutex_lock(&mutex);
    pthread_cond_wait(&conditions[i], &mutex);
    pthread_mutex_unlock(&mutex);

    threads_id[i] = 1;

    struct sockaddr_in clientaddr;
    socklen_t addrlen;
    addrlen = sizeof(clientaddr);

    int rcvd, fd, bytes_read;
    char* ptr;

    //Waits for a client
    printf("Waiting for client in thread %d\n\n",i+1);
    int client = accept(listenfd, (struct sockaddr *) &clientaddr, &addrlen);
    if (client < 0){
        perror("accept");
        threads_id[i] = 0;
        threads_available = threads_available + 1;
        pthread_mutex_lock(&mutex);
        pthread_cond_signal(&conditions[thread_amount]);
        pthread_mutex_unlock(&mutex);
        attend_client((void*) i);
    }

    //At this point, a client is connected
    threads_available = threads_available - 1;

    printf("Client connected on thread %d\n\n",i+1);

    //Tells the server to put another child to receive a client
    pthread_mutex_lock(&mutex);
    pthread_cond_signal(&conditions[thread_amount]);
    pthread_mutex_unlock(&mutex);

    struct PACKET *np = (struct PACKET *)malloc(sizeof(struct PACKET)), *hp;
    int buffer;
    buffer = recv(client, np, sizeof(struct PACKET), 0);
    //Receives the message from the client
    // receive error
    if (buffer < 0){
        perror("recv");
        threads_id[i] = 0;
        threads_available = threads_available + 1;
        if(threads_available == 1){
            pthread_mutex_lock(&mutex);
            pthread_cond_signal(&conditions[thread_amount]);
            pthread_mutex_unlock(&mutex);
        }
        attend_client((void*) i);
    }

    // receive socket closed
    if (buffer==0){
        perror("Client disconnected upexpectedly");
        threads_id[i] = 0;
        threads_available = threads_available + 1;
        if(threads_available == 1){
            pthread_mutex_lock(&mutex);
            pthread_cond_signal(&conditions[thread_amount]);
            pthread_mutex_unlock(&mutex);
        }
        attend_client((void*) i);
    }
        // message received
    hp = ntohp(np);
    printf("[%d]command #%d\n",client, hp->commid);
    switch(hp->commid){
            case GET:
                client_get(hp, client);
                break;
            case PUT:
                client_put(hp, client);
                break;
            case LS:
                client_ls(hp, client);
                break;
            /*case CD:
                client_cd(hp, client);
                break;
                case QUIT:
                    client_quit(hp, threadinfo);
                    break;*/
            default:
                break;
    }
    //Closing SOCKET
    shutdown(client, SHUT_RDWR);
    close(client);
    threads_id[i] = 0;
    threads_available = threads_available + 1;

    //When before this poitn, every thread was busy, tells the server
    //that this thread is ready to receive a client
    if(threads_available == 1){
        pthread_mutex_lock(&mutex);
        pthread_cond_signal(&conditions[thread_amount]);
        pthread_mutex_unlock(&mutex);
    }
    printf("Client disconnected from thread %d\n\n",i+1);
    attend_client((void*) i);
}

//ls Command.................................................................................................
void client_ls(struct PACKET *hp, int client){

    struct PACKET *np;
    int sent;
    //char cwd[DATALEN];
    DIR *dir;
    struct dirent *e;

    if((dir = opendir(root)) != NULL){
        while((e = readdir(dir)) != NULL){
            strcpy(hp->data, e->d_name);
            //strcat(hp->data, "\n");
            hp->flag = OK;
            hp->len = strlen(hp->data);
            np = htonp(hp);
            sent = send(client, np, sizeof(struct PACKET), 0);
        }
        hp->flag = DONE;
        strcpy(hp->data, "");
        hp->len = strlen(hp->data);
        np = htonp(hp);
        sent = send(client, np, sizeof(struct PACKET), 0);
    }else
        hp->flag = ERR;

    if(hp->flag == ERR){
        strcpy(hp->data, "Error processing 'ls' command at server..!!\n\n");
        hp->len = strlen(hp->data);
        np = htonp(hp);
        sent = send(client, np, sizeof(struct PACKET), 0);
        //exit(1);
    }

}


//get Command..................................................................................................
void client_get(struct PACKET *hp, int client){
    struct PACKET *np;
    int sent, total_sent = 0;
    size_t read;
    FILE *in;
    char path[496];
    strcpy(path,root);
    strcat(path, "/");
    strcat(path,hp->data);
    printf("File:%s\n", path);
    in = fopen(path, "rb");

    if(!in){

        hp->flag = ERR;
        strcpy(hp->data, "Error opening file in server!\n\n");
        hp->len = strlen(hp->data);
        np = htonp(hp);
        sent = send(client, np, sizeof(struct PACKET), 0);


        fprintf(stderr, "Error opening file:%s\n\n", hp->data);
        return;
    }


    sendFile(hp, client, in);

    fclose(in);
}

//put Command...................................................................................................
void client_put(struct PACKET *hp, int client){
    struct PACKET *np = (struct PACKET *)malloc(sizeof(struct PACKET));
    int bytes, total_recv = 0;
    //getFileNameFromPath(hp->data);
    FILE *out;
    char path[496];
    strcpy(path,root);
    strcat(path, "/");
    strcat(path,hp->data);
    printf("File:%s\n", path);
    out = fopen(path, "wb");
    if(!out){

        hp->flag = ERR;
        strcpy(hp->data, "Error creating file in server!\n\n");
        hp->len = strlen(hp->data);
        np = htonp(hp);
        bytes = send(client, np, sizeof(struct PACKET), 0);

        fprintf(stderr, "Error creating file:%s\n\n", hp->data);
        //return;
    }else{

        hp->flag = READY;
        //strcpy(hp->data,"");
        hp->len = strlen(hp->data);
        np = htonp(hp);


        bytes = send(client, np, sizeof(struct PACKET), 0);

        //Once file is created at server
        recvFile(hp, np,client, out);

    }

    fclose(out);
}


//Function that handles clients when every thread is busy
void out_of_threads(){
    int pid;
    if ((pid = fork()) < 0){
        perror("fork");
        exit(1);
    }

    //Fork that receives clients, tells them that there is no thread available
    if (pid == 0){ /* child */
        struct sockaddr_in clientaddr;
        socklen_t addrlen;
        addrlen = sizeof(clientaddr);
        int client;

        while(1){
            client = accept(listenfd, (struct sockaddr *) &clientaddr, &addrlen);
            if (client < 0){
                perror("accept");
            }
            send(client, "HTTP/1.0 500 Internal Server Error\r\n", 35, 0);
            send(client, "<html><body><h1>Server full</h1></body></html>", 46, 0);
            shutdown(client, SHUT_RDWR);         //All further send and recieve operations are DISABLED...
            close(client);
        }
    }
        //Waits until a thread is free
    else{
        printf("All threads are busy\n");

        pthread_mutex_lock(&mutex);
        pthread_cond_wait(&conditions[thread_amount], &mutex);
        pthread_mutex_unlock(&mutex);

        kill(pid, SIGKILL);

        printf("One thread is now free to receive client\n");
    }
}

int main(int argc, char* argv[]){
    pthread_mutex_init(&mutex, NULL);
    thread_amount = atoi(argv[2]);
    root = argv[4];
    port = atoi(argv[6]);
    recieve_clients();
    return 0;
}

