#include "ftpclient.h"

// np = network packet
// PACKET es un struct con la informacion necesaria para ejecutar un comando.
struct PACKET *np;
//host tiene el formato ip:puerto se recibe por argumentos al correr el programa.
char* host;
//Se obtiene a partir del host, se convierte a int para crear el address.
int PORTSERVER;


int main(int argc, char* argv[]){
    host = argv[2];
    //delimiter es una variable para saber como separar el ip con el puerto a partir del host.
    char delimiter[] = ":";
    //serv_addr es el socket del cliente se abre conectandose a la direccion del host.
    struct sockaddr_in serv_addr;
    // command es un struct que posee informacion del comando a ejecutar.
    struct command *Command;

	int client_socket;
    //estructura protoent es una estructura que contiene el nombre y los números de un protocolo dado.
	struct protoent *protoent;
    // se selecciona el protocolo tcp para intercambio de datos.
    protoent = getprotobyname("tcp");
    // si no hay respuesta presentar error
    if (protoent == NULL) {
        perror("getprotobyname");
        exit(EXIT_FAILURE);
    }
    // se manda el numero de protocolo obtenido en tipo host byte (p_proto).
    client_socket = socket(AF_INET, SOCK_STREAM, protoent->p_proto);
    //se contempla error en el socket.
    if (client_socket == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    //-------------------------------------------Crear Address-----------------------------------------//
    // ptr es un puntero a host para obtener el ip.
    char *ptr = strtok(host,delimiter);
    memset((char*) &serv_addr, 0, sizeof(struct sockaddr_in));
    serv_addr.sin_addr.s_addr = inet_addr(ptr);
    ptr = strtok(NULL,delimiter);
    // se obtiene ahora con ptr el puerto.
    PORTSERVER = atoi(ptr);
    //AF = Address family para una ip especifica.
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORTSERVER);

    //Se conecta el socket del cliente a el address del servidor.
    if (connect(client_socket, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    printf("Connected with server in address: %s and port: %d\n",host,PORTSERVER);

	printf("FTP Client started : \n");
	// variable para obtener el input del usuario para los comandos.
	char userInput[U_INPUTLEN];
	while(1){
		printf("$>");
		//Se obtiene el input.
		fgets(userInput, U_INPUTLEN, stdin);
		char *pos;
		if((pos = strchr(userInput, '\n')) != NULL)
			*pos = '\0';
		//Se parsea el input del usuario y se pasa los datos a el struct command.
		Command = parseInput(userInput);

		//Switch para redireccionar el comando del input a la funcion esperada.
		switch(Command->id){
			case LS:
				server_ls(client_socket);
				break;
			case CD:
				server_cd(Command, client_socket);
				break;
			case GET:
				server_get(Command, client_socket);
				break;
			case PUT:
				server_put(Command, client_socket);
				break;	
			case QUIT:
				client_QUIT(Command, client_socket);
                close(client_socket);
                printf("\n\nConection Closed...\n");
                fflush(stdout);
                return 0;
			default:
				break;
		}
	}

	return 0;
}



//Comando ls
void server_ls(int client_socket){

    //Se prepara el struct para enviar al servidor el comando y los datos de este.
    int bytes, sent;
	struct PACKET *hp = (struct PACKET *)malloc(sizeof (struct PACKET));
	hp->flag = OK;
	strcpy(hp->data, "");
	hp->len = strlen(hp->data);
	hp->commid = LS;
    //Se convierte el struct packet a tipo network para poder ser enviado.
	np = htonp(hp);

	//Se contempla eror de envio de estructura packet al servidor.
	if((sent = send(client_socket, np, size_packet, 0)) < 0){
		fprintf(stderr, "Error sending packets !!\n");
		return;
	}
    //Se contempla errores de respuesta.
	if((bytes = recv(client_socket, np, size_packet, 0)) <= 0){
		fprintf(stderr, "Error receiving packet !!\n");
		return;
	}
	//Se convierte el packet a tipo host para poder ser leido.
	hp = ntohp(np);
	//Se muestra los datos solicitados por el cliente.
	while(hp->flag != DONE){
		printf("%s\n", hp->data);
		//Se continua recibiendo los directorios y archivos en el directorio seleccionado.
		if((bytes = recv(client_socket, np, size_packet, 0)) <= 0){
			fprintf(stderr, "Error receiving packet !!\n");
			return;
		}
		hp = ntohp(np);	
	}
}


//Comando cd
void server_cd(struct command *cmd, int client_socket){

    //Se prepara el struct para enviar al servidor el comando y los datos de este.
	struct PACKET *hp = (struct PACKET *)malloc(sizeof (struct PACKET));
	int bytes, sent;

	hp->flag = OK;
	hp->commid = CD;
	strcpy(hp->data, cmd->path);
	hp->len = strlen(hp->data);
    //Se convierte el struct packet a tipo network para poder ser enviado.
	np = htonp(hp);

    //Se contempla eror de envio de estructura packet al servidor.
	if((sent = send(client_socket, np, size_packet, 0)) < 0){
		fprintf(stderr, "Error sending packets !!\n");
		return;
	}
    //Se contempla errores de respuesta.
	if((bytes = recv(client_socket, np, size_packet, 0)) <= 0){
		fprintf(stderr, "Error receiving packet..!!\n");
		return;
	}
    //Se convierte el packet a tipo host para poder ser leido.
	hp = ntohp(np);
	//Se le informa al cliente el directorio al cual se solicito cambiar.
	printf("Current Server directory :\n%s\n", hp->data);
}

//Comando get
void server_get(struct command *cmd, int client_socket){
    //Se verifica si se obtiene los archivos necesarios para abrir el archivo solicitado.
	FILE *out = fopen(cmd->fileName, "wb");
	if(!out){
		fprintf(stderr, "Error getting file. See if required permissions are satisfied !!\n");
		return;
	}

    //Se prepara el struct para enviar al servidor el comando y los datos de este.
	struct PACKET *hp = (struct PACKET *)malloc(sizeof (struct PACKET));
	int sent, bytes;
	hp->commid = GET;
	hp->flag = OK;
	strcpy(hp->data, cmd->path);
	hp->len = strlen(hp->data);

    //Se convierte el struct packet a tipo network para poder ser enviado.
	np = htonp(hp);

    //Se contempla eror de envio de estructura packet al servidor.
	if((sent = send(client_socket, np, size_packet, 0)) < 0){
		fprintf(stderr, "Error sending packets !!\n");
		return;
	}
    //Se llama a la funcion para recibir un archivo.
	recvFile(hp, np, client_socket, out);
	//Se cierra el archivo.
	fclose(out);
}

//Comando put
void server_put(struct command *cmd, int client_socket){

    //Se abre el archivo que se quiere enviar al cliente.
	FILE *in = fopen(cmd->path, "rb");
	if(!in){
		fprintf(stderr, "Error opening file. See if pemissions are satisfied !!\n");
		return;
	}

    //Se prepara el struct para enviar al servidor el comando y los datos de este.
	struct PACKET *hp = (struct PACKET *)malloc(sizeof (struct PACKET));
	int sent, bytes;
	hp->commid = PUT;
	hp->flag = OK;
	strcpy(hp->data, cmd->fileName);
	hp->len = strlen(hp->data);

    //Se convierte el struct packet a tipo network para poder ser enviado.
	np = htonp(hp);

    //Se contempla eror de envio de estructura packet al servidor.
	if((sent = send(client_socket, np, size_packet, 0)) <= 0){
		fprintf(stderr, "Error sending packets !!\n");
		return;
	}
    //Se contempla errores de respuesta.
	if((bytes = recv(client_socket, np, size_packet, 0)) <= 0){
		fprintf(stderr, "Error receiving confirmation packets !!\n");
		return;
	}

    //Se convierte el struct packet a tipo host para poder ser leido.
	hp = ntohp(np);

	//Se verifica por medio del flag si se puede enviar el archivo al cliente.
	//De lo contrario se muestra un errror.
	if(hp->flag == READY){
		sendFile(hp, client_socket, in);
	}else{
		fprintf(stderr, "Error creating file at server !!\n");
	}
	//Se cierra el archivo.
	fclose(in);
}


//Se parsea el input del cliente.
struct command * parseInput(char *input){
	char n[DATALEN+2];
	strcpy(n, "");
	strcat(n, " ");
	strcat(n, input);
	strcat(n, " ");
	char *option, *last;
	//Se prepara el commando en valores default para poder llenarlos despues.
	struct command *cmd = (struct command *)malloc(sizeof (struct command));
	strcpy(cmd->path, "");
	strcpy(cmd->fileName, "");
	cmd->id = -2;

	//Se coloca en la opcion el primer espacio de la cadena de caracteres (este es el comando)
	option = strtok(input, " \t");
	//Se coloca en el struct command id el comando solicitado por el cliente.
	if(!strcmp(option, "ls")){
		cmd->id = LS;
	}

	else if(!strcmp(option, "cd")){
		cmd->id = CD;
		option = strtok(NULL, " ");
		strcpy(cmd->path, option);

	}

	else if(!strcmp(option, "get")){
		cmd->id = GET;
		option = strtok(NULL, " ");
		strcpy(cmd->path, option);
		while(option != NULL){
			last = option;
			option = strtok(NULL, "/");
		}
		strcpy(cmd->fileName, last);
	}
	else if(!strcmp(option, "put")){
		cmd->id = PUT;
		option = strtok(NULL, " ");
		strcpy(cmd->path, option);
		while(option != NULL){
			last = option;
			option = strtok(NULL, "/");
		}
		strcpy(cmd->fileName, last);

	}else if(!strcmp(option, "quit")){
		cmd->id = QUIT;

	}else
		fprintf(stderr,"Incorrect command. Try Again\n\n");
	
	return cmd;
}

//Comando quit
void client_QUIT(struct command *cmd, int client_socket){
    //Se prepara el struct para enviar al servidor el comando y los datos de este.
	struct PACKET *hp = (struct PACKET *)malloc(sizeof(struct PACKET));
	hp->flag = QUIT;
	hp->commid = QUIT;
	strcpy(hp->data, "");
	hp->len = 0;

    //Se convierte el struct packet a tipo network para poder ser enviado.
	np = htonp(hp);
    //Se envia la solicitud para cerrar el cliente.
	int sent = send(client_socket, np, size_packet, 0);

}