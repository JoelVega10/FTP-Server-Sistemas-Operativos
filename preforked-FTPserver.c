#define _XOPEN_SOURCE 600
#include "preforked-FTPserver.h"
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


int* forks_id;
int* forks_id_backup;
int fork_amount, current_fork, listenfd, port, new_client, fork_free;
char* root;



//Function that contorls the processes
void recieve_clients(){
    int slot = 0;
    
    start_server();
    // Ignore SIGCHLD to avoid zombie threads
    signal(SIGCHLD,SIG_IGN);
    precreate_forks();
    //Signals that the children will send to the parent
    //SIGCONT: A new client is connected
    //SIGCHLD: A client exited and the current child is ready to receive more clients
    signal(SIGCONT, pass);
    
    struct sigaction action;
    action.sa_flags = SA_SIGINFO;
    action.sa_sigaction = &fork_now_free;

    if (sigaction(SIGCHLD, &action, NULL) == -1) {
        perror("sigaction");
        exit(1); 
    }
    
    // Accept connections
    while (1){
        printf("Accepting Connections\n");
        //Tells the next empty child to wait for a new client
        kill(forks_id[slot], SIGCONT);
        forks_id[slot] = 0;
        new_client = 0;
        
        //Waits until a new client is connected to the child
        while (new_client == 0){
            pause();
        }
        
        //Get a free child, waits until one of them is free
        slot = check_forks_available();
        
        //When every child is busy
        if(slot == -1){
            out_of_forks();
            slot = check_forks_available();
        }
    }
}

//A new client is connected
void pass(){
    signal(SIGCONT, pass);
    new_client = 1;
}

//One child is now free
void fork_now_free(int signo, siginfo_t* info, void* extra){
    int i = 0;
    int found = 0;
    
    //When the process that sent the signal is not one of the childre, ignore the signal
    while(found == 0 && i < fork_amount){
        if(forks_id_backup[i] == info->si_pid){
            found = 1;
        }
        i = i + 1;
    }
    
    //Set the child as free to recieve a new client
    if(found == 1){
        i = info->si_value.sival_int;
        forks_id[i] = forks_id_backup[i];
        fork_free = 1;
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

//Function tha precreate the processes to be used to receive clients
void precreate_forks(){
    forks_id = (int*)malloc(sizeof(int) * fork_amount);
    forks_id_backup = (int*)malloc(sizeof(int) * fork_amount);
    
    int i = 0;
    int pid;
    
    while(i < fork_amount){
        if ((pid = fork()) < 0){ 
            perror("fork"); 
            exit(1); 
        }
        
        //child, waits until the server tells him to receive a client
        if (pid == 0){ 
            signal(SIGCONT, attend_client);
            current_fork = i;
            pause(); 
        } 
  
        else{
            forks_id[i] = pid;
            forks_id_backup[i] = pid;
        } 
        i = i + 1;
    }
}

//Function that checks if there is a child available
//Returns the position in the array if one is found
//If there is no child available, -1 is returned
int check_forks_available(){
    int answer = -1;
    int i = 0;
    
    while(answer == -1 && i < fork_amount){
        if(forks_id[i] != 0){
            answer = i;
        }
        i = i + 1;
    }
    
    return answer;
}

//Child function, to attend a client
void attend_client(){
    signal(SIGCONT, attend_client);
    
    struct sockaddr_in clientaddr;
    socklen_t addrlen;
    addrlen = sizeof(clientaddr);
    
    union sigval value;
    value.sival_int = current_fork;
    
    int rcvd, fd, bytes_read;
    char message[100];
    char* ptr;
    sprintf(message, "%d", current_fork);
    
    //Waits for a client
    printf("Waiting for client in process %d\n\n",current_fork+1); 
    int client = accept(listenfd, (struct sockaddr *) &clientaddr, &addrlen);
    if (client < 0){
        perror("accept"); 
        sigqueue(getppid(), SIGCHLD, value);
        pause();
        attend_client();
    }
    
    //At this point, a client is connected
    printf("Client connected on process %d\n\n",current_fork+1);
    
    //Tells the server to put another child to receive a client
    kill(getppid(), SIGCONT);
    manage_client(client,value);
}

void manage_client(int client, union sigval value) {
    struct PACKET *np = (struct PACKET *) malloc(sizeof(struct PACKET)), *hp;
    int buffer;
    buffer = recv(client, np, sizeof(struct PACKET), 0);
    //Receives the message from the client
    // receive error
    if (buffer < 0) {
        perror("recv");
        sigqueue(getppid(), SIGCHLD, value);
        pause();
        attend_client();
    }

    // receive socket closed
    if (buffer==0){
        perror("Client disconnected upexpectedly");
        sigqueue(getppid(), SIGCHLD, value);
        pause();
        attend_client();
    }

    // message received
    hp = ntohp(np);
    printf("[%d] Command #%d\n",client, hp->commid);
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
        case CD:
            client_cd(hp, client);
            break;
        case QUIT:
            close_socket_client(client,value);
            break;
    }
    manage_client(client,value);
}



void close_socket_client(int client,union sigval value){
    //Closing SOCKET
    shutdown(client, SHUT_RDWR);
    close(client);
    printf("Client disconnected from process %d\n\n",current_fork+1);
    sigqueue(getppid(), SIGCHLD, value);
    pause();
    attend_client();
}

//Function that handles clients when every child is busy
void out_of_forks(){
    int pid;
    if ((pid = fork()) < 0){ 
        perror("fork"); 
        exit(1); 
    }
    
    //Fork that receives clients, tells them that there is no child available
    if (pid == 0){ /* child */
        struct sockaddr_in clientaddr;
        socklen_t addrlen;
        addrlen = sizeof(clientaddr);
        int client;
        while(1){
            if (client < 0){
                perror("accept"); 
                pause();
            }
            send(client, "HTTP/1.0 500 Internal Server Error\r\n", 35, 0);
            send(client, "<html><body><h1>Server full</h1></body></html>", 46, 0);
            shutdown(client, SHUT_RDWR);         //All further send and recieve operations are DISABLED...
            close(client);
        }
    }
    else{
        //Waits until a child is free
        printf("All processes are busy\n");
        
        fork_free = 0;
        
        while (fork_free == 0){
            pause();
        }
        
        kill(pid, SIGKILL);
        printf("One process is now free to receive client\n");
    }
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


//cd Command.........................................................................................
void client_cd(struct PACKET *hp, int client){
    struct PACKET *np;
    int sent;
    char temp[DATALEN];
    getcwd(temp, DATALEN);
    chdir(root);
    if(chdir(hp->data) < 0){
        hp->flag = ERR;
        strcpy(hp->data, "Error changing directory, probably non-existent\n");
        hp->len = strlen(hp->data);
        np = htonp(hp);
        sent = send(client, np, sizeof(struct PACKET), 0);
    }

    else{
        getcwd(root, DATALEN);
        chdir(temp);
        client_pwd(hp, client);
    }

}

//pwd Command...............................................................................................
void client_pwd(struct PACKET *hp, int client){

    int sent;
    struct PACKET *np;

    strcpy(hp->data, root);
    hp->flag = OK;
    hp->len = strlen(hp->data);
    np = htonp(hp);
    sent = send(client, np, sizeof(struct PACKET), 0);
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

int main(int argc, char* argv[]){
    fork_amount = atoi(argv[2]);
    root = argv[4];
    port = atoi(argv[6]);
    recieve_clients();
    return 0;
}
