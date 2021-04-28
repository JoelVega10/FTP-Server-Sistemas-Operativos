#define _XOPEN_SOURCE 600
#define SO_REUSEPORT 15
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
#include <pthread.h>

//Variables globales para manejar los hilos
pthread_t threads;
pthread_mutex_t mutex;
pthread_cond_t* conditions;

int* threads_id;
int thread_amount, current_fork, listenfd, port, threads_available;
char* root;


//Funcion que recibe las conexiones de los clientes
void recieve_clients(){
    //Slot es la posicion de los id de los hilos, para informar que esta ocupado.
    int slot = 0;
    //Se inicia el server
    start_server();
    // Se coloca ignorar SIGCHLD para evitar hilos zombie
    //Hilos Zombie = El hilo padre no espera que los hijos terminen y no se toma el exit de los hijos.
    signal(SIGCHLD,SIG_IGN);
    precreate_threads();

    // Acepta conexiones
    while (1){
        printf("Accepting Connections\n");
        //Coloca el hilo en slot como ocupado.
        threads_id[slot] = 1;

        //Le dice al siguiente hilo vacio que espere otro cliente.
        pthread_mutex_lock(&mutex);
        pthread_cond_signal(&conditions[slot]);
        pthread_mutex_unlock(&mutex);

        //Espera hasta que otro cliente este conectado.
        pthread_mutex_lock(&mutex);
        pthread_cond_wait(&conditions[thread_amount], &mutex);
        pthread_mutex_unlock(&mutex);

        //Si no hay hilos disponibles espera a que se desocupe uno.
        if(threads_available == 0){
            out_of_threads();
        }

        //Obtiene un hilo vacio.
        slot = check_threads_available();
    }
}

//Inicia el server (socket)
void start_server() {
    //Address como su nombre lo dice forma la ruta del host.
    struct sockaddr_in address;

    // Crea el socket que escucha a los clientes.
    if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        printf("error creating control socket : Returning Error No:%d\n", errno);
        return errno;
    }
    //AF = Address family para una ip especifica.
    address.sin_family = AF_INET;
    // Se coloca el puerto especificado por el usuario.
    address.sin_port = htons(port);
    //Se coloca el ip default ftp.
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    memset(&(address.sin_zero), 0, 8);

    //Se contemplan los errores al colocar el socket en el puerto indicado.
    if(bind(listenfd, (struct sockaddr *)&address, sizeof(struct sockaddr)) == -1){
        printf("Error binding control socket to port %d : Returning error No:%d\n", port, errno);
        return errno;
    }


    //BACKLOG sirve para saber cuantas conexiones puede tener en cola sin estar aceptado en el servidor.
    //Se contempla errores al esuchar en el puerto indicado.
    if(listen(listenfd, BACKLOG) == -1){
        printf("Error listening at control port : Returning Error No:%d\n", errno);
        return errno;
    }
    printf("\nServer started at port:%d\n", port);

}

//Crea el numero de hilos que el usuario solicita.
void precreate_threads(){

    //Prepara las variables necesarias para pre-crear los hilos.
    threads_id = (int*)malloc(sizeof(int) * thread_amount);
    conditions = (pthread_cond_t*)malloc(sizeof(pthread_cond_t) * (thread_amount+1));
    threads_available = thread_amount;
    //Se crean los hilos.
    int i = 0;
    while(i < thread_amount){
        pthread_create(&threads, NULL, &attend_client, i);
        pthread_cond_init (&conditions[i], NULL);
        threads_id[i] = 0;
        i = i + 1;
    }
    pthread_cond_init (&conditions[i], NULL);
}

//Verifica si hay un hilo disponible.
//Si lo encuentra retorna el numero de hilo.
//Si no hay hilos retorna -1.
int check_threads_available(){
    int answer = -1;
    int i = 0;
    //Busca en la lista de hilos si hay uno disponible.
    while(answer == -1 && i < thread_amount){
        if(threads_id[i] == 0){
            answer = i;
        }
        i = i + 1;
    }

    return answer;
}

//Atiende a los clientes.
void attend_client(void* arg){
    int i = (int) arg;

    //Espera al servidor para decirle al hilo que acepte clientes.
    pthread_mutex_lock(&mutex);
    pthread_cond_wait(&conditions[i], &mutex);
    pthread_mutex_unlock(&mutex);
    //Coloca el hilo i como ocupado.
    threads_id[i] = 1;

    //Recibe el address del cliente.
    struct sockaddr_in clientaddr;
    socklen_t addrlen;
    addrlen = sizeof(clientaddr);

    int rcvd, fd, bytes_read;
    char* ptr;

    //Espera al cliente
    printf("Waiting for client in thread %d\n\n",i+1);
    //Acepta al cliente.
    int client = accept(listenfd, (struct sockaddr *) &clientaddr, &addrlen);
    //Verifica si hubo un error al aceptar al cliente y coloca el id de hilo como desocupado.
    if (client < 0){
        perror("accept");
        threads_id[i] = 0;
        threads_available = threads_available + 1;
        pthread_mutex_lock(&mutex);
        pthread_cond_signal(&conditions[thread_amount]);
        pthread_mutex_unlock(&mutex);
        attend_client((void*) i);
    }

    //El cliente se conecta.
    threads_available = threads_available - 1;

    printf("Client connected on thread %d\n\n",i+1);

    //Coloca otro hilo a recibir clientes si es posible.
    pthread_mutex_lock(&mutex);
    pthread_cond_signal(&conditions[thread_amount]);
    pthread_mutex_unlock(&mutex);
    manage_client(client,i);
}

//Espera comandos del cliente hasta recibir un quit.
void manage_client(int client,int i){
    //con np es la estructura para comunicar la informacion con el cliente.
    struct PACKET *np = (struct PACKET *)malloc(sizeof(struct PACKET)), *hp;
    int buffer;
    //buffer recibe el mensaje del cliente.
    buffer = recv(client, np, sizeof(struct PACKET), 0);
    //recibe mensajes del cliente

    //si el mensaje presenta un error.
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

    // recibe que el socket se cerro de manera inesperada.
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
    // mensaje recibido del cliente.
    hp = ntohp(np);
    switch(hp->commid){
        case GET:
            printf("[%d] Command #GET\n",client);
            client_get(hp, client);
            break;
        case PUT:
            printf("[%d] Command #PUT\n",client);
            client_put(hp, client);
            break;
        case LS:
            printf("[%d] Command #LS\n",client);
            client_ls(hp, client);
            break;
        case CD:
            printf("[%d] Command #CD\n",client);
            client_cd(hp, client);
            break;
        case QUIT:
            printf("[%d] Command #QUIT\n",client);
            close_socket_client(client,i);
            break;
    }
    manage_client(client,i);
}

//Funcion que cierra la conexion con el cliente.
void close_socket_client(int client, int i){
    shutdown(client, SHUT_RDWR);
    close(client);
    threads_id[i] = 0;
    threads_available = threads_available + 1;

    if(threads_available == 1){
        pthread_mutex_lock(&mutex);
        pthread_cond_signal(&conditions[thread_amount]);
        pthread_mutex_unlock(&mutex);
    }
    printf("Client disconnected from thread %d\n\n",i+1);
    attend_client((void*) i);
}

//comando ls
void client_ls(struct PACKET *hp, int client){

    //paquete para comunicarse con el cliente
    struct PACKET *np;
    int sent;
    //dirent del include dirent.h contiene informacion de un archivo.
    DIR *dir;
    struct dirent *e;

    //verifica si el directorio no esta vacio.
    //Si no esta vacio copia a la estructura packet la informacion necesaria para realizar un ls.
    // Y la envia al cliente.
    if((dir = opendir(root)) != NULL){
        while((e = readdir(dir)) != NULL){
            strcpy(hp->data, e->d_name);
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
    //Si existe un error en el proceso se notifica al cliente.
    if(hp->flag == ERR){
        strcpy(hp->data, "Error processing 'ls' command at server..!!\n\n");
        hp->len = strlen(hp->data);
        np = htonp(hp);
        sent = send(client, np, sizeof(struct PACKET), 0);
    }

}


//comando cd
void client_cd(struct PACKET *hp, int client){
    //paquete para comunicarse con el cliente
    struct PACKET *np;
    int sent;
    //Se cambia al directorio actual del servidor.
    char temp[DATALEN];
    getcwd(temp, DATALEN);
    chdir(root);
    //Se revisa si el directoria al cual se quiere cambiar existe.
    if(chdir(hp->data) < 0){
        hp->flag = ERR;
        strcpy(hp->data, "Error changing directory, probably non-existent\n");
        hp->len = strlen(hp->data);
        np = htonp(hp);
        sent = send(client, np, sizeof(struct PACKET), 0);
    }
    //El directorio existe, por lo cual se cambia a este y se notifica al cliente por medio de pwd.
    else{
        getcwd(root, DATALEN);
        chdir(temp);
        client_pwd(hp, client);
    }

}

//comando pwd
void client_pwd(struct PACKET *hp, int client){
    //paquete para comunicarse con el cliente
    int sent;
    struct PACKET *np;

    //Se copia a los datos del paquete de comunicacion el nuevo root del servidor y se envia.
    strcpy(hp->data, root);
    hp->flag = OK;
    hp->len = strlen(hp->data);
    np = htonp(hp);
    sent = send(client, np, sizeof(struct PACKET), 0);
}



//comando get
void client_get(struct PACKET *hp, int client){
    //paquete para comunicarse con el cliente
    struct PACKET *np;
    int sent, total_sent = 0;
    size_t read;
    FILE *in;
    //Se obtiene el path del archivo a obtener.
    char path[496];
    strcpy(path,root);
    strcat(path, "/");
    strcat(path,hp->data);
    printf("File:%s\n", path);
    in = fopen(path, "rb");
    //Se verifica que el archivo este o se pueda abrir.
    if(!in){
        hp->flag = ERR;
        strcpy(hp->data, "Error opening file in server!\n\n");
        hp->len = strlen(hp->data);
        np = htonp(hp);
        sent = send(client, np, sizeof(struct PACKET), 0);
        fprintf(stderr, "Error opening file:%s\n\n", hp->data);
        return;
    }
    //El archivo se encuentra, se envia hacia el cliente.
    sendFile(hp, client, in);
    //Se cierra el archivo.
    fclose(in);
}

//comando put
void client_put(struct PACKET *hp, int client){

    //paquete para comunicarse con el cliente
    struct PACKET *np = (struct PACKET *)malloc(sizeof(struct PACKET));
    int bytes, total_recv = 0;
    //Se obtiene el path del archivo a enviar al servidor.
    FILE *out;
    char path[496];
    strcpy(path,root);
    strcat(path, "/");
    strcat(path,hp->data);
    printf("File:%s\n", path);
    out = fopen(path, "wb");
    //Si el archivo no existe se presenta el error.
    if(!out){
        hp->flag = ERR;
        strcpy(hp->data, "Error creating file in server!\n\n");
        hp->len = strlen(hp->data);
        np = htonp(hp);
        bytes = send(client, np, sizeof(struct PACKET), 0);
        fprintf(stderr, "Error creating file:%s\n\n", hp->data);
    }else{
        //Se prepara la informacion a enviar.
        hp->flag = READY;
        hp->len = strlen(hp->data);
        np = htonp(hp);
        bytes = send(client, np, sizeof(struct PACKET), 0);

        //Se envia el archivo.
        recvFile(hp, np,client, out);

    }
    //Se cierra el archivo.
    fclose(out);
}




//Funcion que controla si se acaban los hilos.
void out_of_threads(){
    //Proccess id
    int pid;
    if ((pid = fork()) < 0){
        perror("fork");
        exit(1);
    }

    //fork que recibe el cliente y notifica que no hay mas hilos.
    if (pid == 0){
        struct sockaddr_in clientaddr;
        socklen_t addrlen;
        addrlen = sizeof(clientaddr);
        int client;

        while(1){
            client = accept(listenfd, (struct sockaddr *) &clientaddr, &addrlen);
            if (client < 0){
                perror("accept");
            }
            send(client, "No hay hilos disponibles... \r\n", 35, 0);
            shutdown(client, SHUT_RDWR);
            close(client);
        }
    }
        //Espera hasta que exista un hilo libre.
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

