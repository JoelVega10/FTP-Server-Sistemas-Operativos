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

//Variables globales para manejar los procesos
int* forks_id;
int* forks_id_backup;
int fork_amount, current_fork, listenfd, port, new_client, fork_free;
char* root;



//Funcion que recibe las conexiones de los procesos
void recieve_clients(){
    //Slot es la posicion de los id de los hilos, para informar que esta ocupado.
    int slot = 0;
    //Se inicia el server
    start_server();
    // Se coloca ignorar SIGCHLD para evitar hilos zombie
    //Hilos Zombie = El hilo padre no espera que los hijos terminen y no se toma el exit de los hijos.
    signal(SIGCHLD,SIG_IGN);
    precreate_forks();
    //Senales que los hijos enviaran al padre.
    //SIGCONT: Un nuevo cliente esta conectado.
    //SIGCHLD: Un cliente salio y esta un hijo esta listo para recibir mas.
    signal(SIGCONT, pass);

    //Sigaction se utiliza para cambiar la acción realizada por
    //un proceso al recibir una señal específica.
    struct sigaction action;
    action.sa_flags = SA_SIGINFO;
    action.sa_sigaction = &fork_now_free;

    //Significa que no existe accion, o presento un error.
    if (sigaction(SIGCHLD, &action, NULL) == -1) {
        perror("sigaction");
        exit(1); 
    }
    
    // Acepta conexiones
    while (1){
        printf("Accepting Connections\n");
        //Le dice al siguiente hijo libre que espere por un cliente.
        kill(forks_id[slot], SIGCONT);
        forks_id[slot] = 0;
        new_client = 0;

        //Espera hasta que un nuevo cliente se conecte al hijo.
        while (new_client == 0){
            pause();
        }

        //Obtiene un hijo libre, hasta que uno de estos lo este
        slot = check_forks_available();
        
        //Si los hijos estan ocupados.
        if(slot == -1){
            out_of_forks();
            slot = check_forks_available();
        }
    }
}

//Un nuevo cliente se conecto, se anade a la cuenta.
void pass(){
    signal(SIGCONT, pass);
    new_client = 1;
}

//Nos dice que un nuevo hijo esta libre.
void fork_now_free(int signo, siginfo_t* info, void* extra){
    int i = 0;
    int found = 0;

    //Cuando el proceso que envia la senal no es uno de los hijos, se ignora la senal.
    while(found == 0 && i < fork_amount){
        if(forks_id_backup[i] == info->si_pid){
            found = 1;
        }
        i = i + 1;
    }

    //Se coloca el hijo como libre para recibir un cliente.
    if(found == 1){
        i = info->si_value.sival_int;
        forks_id[i] = forks_id_backup[i];
        fork_free = 1;
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

//Funcion que precrea los procesos para recibir clientes.
void precreate_forks(){
    //Prepara las variables necesarias para pre-crear los procesos.
    forks_id = (int*)malloc(sizeof(int) * fork_amount);
    forks_id_backup = (int*)malloc(sizeof(int) * fork_amount);
    
    int i = 0;
    int pid;

    //Se precrean la cantidad de procesos que digito el usuario.
    while(i < fork_amount){
        if ((pid = fork()) < 0){ 
            perror("fork"); 
            exit(1); 
        }

        //El hijo espera hasta que el servidor le diga que reciba un cliente.
        if (pid == 0){ 
            signal(SIGCONT, attend_client);
            current_fork = i;
            pause(); 
        } 
        //Sino se coloca el proceso como ocupado.
        else{
            forks_id[i] = pid;
            forks_id_backup[i] = pid;
        } 
        i = i + 1;
    }
}

//Verifica si hay un proceso disponible.
//Si lo encuentra retorna el numero de proceso.
//Si no hay hilos retorna -1.
int check_forks_available(){
    int answer = -1;
    int i = 0;
    //Busca en la lista de procesos si hay uno disponible.
    while(answer == -1 && i < fork_amount){
        if(forks_id[i] != 0){
            answer = i;
        }
        i = i + 1;
    }
    
    return answer;
}

//Atiende a los clientes.
void attend_client(){
    //Le dice al proceso que continue a atender clientes.
    signal(SIGCONT, attend_client);


    //Recibe el address del cliente.
    struct sockaddr_in clientaddr;
    socklen_t addrlen;
    addrlen = sizeof(clientaddr);

    //Se obtiene el valor del proceso actual.
    union sigval value;
    value.sival_int = current_fork;

    //Valores para recibir mensajes del cliente.
    int rcvd, fd, bytes_read;
    char message[100];
    char* ptr;
    sprintf(message, "%d", current_fork);
    
    //Espera al cliente.
    printf("Waiting for client in process %d\n\n",current_fork+1); 
    int client = accept(listenfd, (struct sockaddr *) &clientaddr, &addrlen);
    if (client < 0){
        perror("accept"); 
        sigqueue(getppid(), SIGCHLD, value);
        pause();
        attend_client();
    }
    
    //El cliente se conecta.
    printf("Client connected on process %d\n\n",current_fork+1);
    
    //Se coloca otro proceso a esperar clientes, si es posible.
    kill(getppid(), SIGCONT);
    manage_client(client,value);
}

//Espera comandos del cliente hasta recibir un quit.
void manage_client(int client, union sigval value) {
    //con np es la estructura para comunicar la informacion con el cliente.
    struct PACKET *np = (struct PACKET *) malloc(sizeof(struct PACKET)), *hp;
    int buffer;
    //buffer recibe el mensaje del cliente.
    buffer = recv(client, np, sizeof(struct PACKET), 0);
    //recibe mensajes del cliente

    //si el mensaje presenta un error.
    if (buffer < 0) {
        perror("recv");
        sigqueue(getppid(), SIGCHLD, value);
        pause();
        attend_client();
    }


    // recibe que el socket se cerro de manera inesperada.
    if (buffer==0){
        perror("Client disconnected upexpectedly");
        sigqueue(getppid(), SIGCHLD, value);
        pause();
        attend_client();
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
            close_socket_client(client,value);
            break;
    }
    manage_client(client,value);
}


//Funcion que cierra la conexion con el cliente.
void close_socket_client(int client,union sigval value){
    shutdown(client, SHUT_RDWR);
    close(client);
    printf("Client disconnected from process %d\n\n",current_fork+1);
    sigqueue(getppid(), SIGCHLD, value);
    pause();
    attend_client();
}

//Funcion que controla si se acaban los hijos.
void out_of_forks(){
    //Proccess id
    int pid;
    if ((pid = fork()) < 0){ 
        perror("fork"); 
        exit(1); 
    }

    //fork que recibe el cliente y notifica que no hay mas hijos.
    if (pid == 0){
        struct sockaddr_in clientaddr;
        socklen_t addrlen;
        addrlen = sizeof(clientaddr);
        int client;
        while(1){
            if (client < 0){
                perror("accept");
                pause();
            }
            send(client, "No hay forks disponibles... \r\n", 35, 0);
            shutdown(client, SHUT_RDWR);
            close(client);
        }
    }
    else{
        //Espera hasta que exista un hijo libre.
        printf("All processes are busy\n");
        
        fork_free = 0;
        
        while (fork_free == 0){
            pause();
        }
        
        kill(pid, SIGKILL);
        printf("One process is now free to receive client\n");
    }
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

int main(int argc, char* argv[]){
    fork_amount = atoi(argv[2]);
    root = argv[4];
    port = atoi(argv[6]);
    recieve_clients();
    return 0;
}
