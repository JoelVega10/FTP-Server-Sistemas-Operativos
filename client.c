#include "client.h"

struct PACKET *np;

int main(){
    struct sockaddr_in serv_addr;
    struct command *uComm;
	int c_sockfd;
    struct protoent *protoent;
    /* Build the socket. */
    protoent = getprotobyname("tcp");
    if (protoent == NULL) {
        perror("getprotobyname");
        exit(EXIT_FAILURE);
    }
    c_sockfd = socket(AF_INET, SOCK_STREAM, protoent->p_proto);

    if (c_sockfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset((char*) &serv_addr, 0, sizeof(struct sockaddr_in));
    serv_addr.sin_addr.s_addr = inet_addr(IPSERVER);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORTSERVER);

    /* Actually connect. */
    if (connect(c_sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    printf("Connected with server in address: %d and port: %d\n",IPSERVER,PORTSERVER);

	printf("FTP Client started : \n");		//User message
	char userInput[U_INPUTLEN];
	while(1){
		printf("ftp>");		//Terminal prompt
		fgets(userInput, U_INPUTLEN, stdin);	//User input
		char *pos;
		if((pos = strchr(userInput, '\n')) != NULL)
			*pos = '\0';
		uComm = getUserCommand(userInput);		//For parsing of user input into struct command

		switch(uComm->id){
			case CPWD:
				getCurrentWorkingDir();	//For client side pwd
				break;
			case CLS:
				listContentsDir();		//For client side ls
				break;
			case CCD:					//For client side cd
				if(chdir(uComm->path)< 0){
					fprintf(stderr, "Error changing directory, probably non-existent..!!\n");
				}
				else{
					printf("Current working client directory is :\n");
					getCurrentWorkingDir();
				}
				break;
			case PWD:			
				server_pwd(c_sockfd);		//For server side pwd
				break;
			case LS:
				server_ls(c_sockfd);		//For server side ls
				break;
			case CD:
				server_cd(uComm, c_sockfd);		//For server side cd
				break;
			case GET:
				server_get(uComm, c_sockfd);		//For get operation
				break;
			case PUT:
				server_put(uComm, c_sockfd);		//For put operation
				break;	
			case QUIT:
				//client_QUIT(uComm, c_sockfd);
				goto outsideLoop;
			default:
				//fprintf(stderr, "Incorrect command..!!\n");
				break;
		}
	}
	outsideLoop:			//label for breaking out of loop

	close(c_sockfd);		//letting server know that socket is closing
	printf("\n\nDONE !!\n");

	fflush(stdout);
	return 0;
}

//Client side pwd............................................................................
void getCurrentWorkingDir(){
	char dir[DATALEN];
	if(!getcwd(dir, DATALEN))
		fprintf(stderr, "Error getting current directory..!!\n");
	else
		printf("%s\n", dir);
}

//Client side ls.............................................................................
void listContentsDir(){
	char cwd[DATALEN];
	DIR *dir;
	struct dirent *e;
	if(!getcwd(cwd, DATALEN)){
		fprintf(stderr, "Error .. !!\n");
		exit(1);
	}else{
		if((dir = opendir(cwd)) != NULL){
			while((e = readdir(dir)) != NULL){
				printf("\n%s", e->d_name);
			}
		}else
			fprintf(stderr, "Error opening: %s !!\n", cwd);
	}
	printf("\n");
	fflush(stdout);
}

//Server side pwd..............................................................................
void server_pwd(int sfd){
	struct PACKET *hp = (struct PACKET *)malloc(sizeof (struct PACKET));
	hp->flag = OK;
	hp->commid = PWD;
	strcpy(hp->data, "");
	hp->len = strlen(hp->data);

	np = htonp(hp);
	int sent, bytes;

	sent = send(sfd, np, size_packet, 0);

	if((bytes = recv(sfd, np, size_packet, 0)) <= 0){
		fprintf(stderr, "Error receiving packet..!!\n");
		return;
	}

	hp = ntohp(np);
	printf("Current Server directory :\n%s\n", hp->data);

}

//Server side ls...............................................................................
void server_ls(int sfd){
	int bytes, sent;
	struct PACKET *hp = (struct PACKET *)malloc(sizeof (struct PACKET));
	hp->flag = OK;
	strcpy(hp->data, "");
	hp->len = strlen(hp->data);
	hp->commid = LS;
	np = htonp(hp);
	if((sent = send(sfd, np, size_packet, 0)) < 0){
		fprintf(stderr, "Error sending packets !!\n");
		return;
	}
	if((bytes = recv(sfd, np, size_packet, 0)) <= 0){
		fprintf(stderr, "Error receiving packet !!\n");
		return;
	}
    printf("Paso 3\n");
	hp = ntohp(np);
	while(hp->flag != DONE){
		printf("%s\n", hp->data);
		if((bytes = recv(sfd, np, size_packet, 0)) <= 0){
			fprintf(stderr, "Error receiving packet !!\n");
			return;
		}
		hp = ntohp(np);	
	}
}


//Server side cd................................................................................
void server_cd(struct command *cmd, int sfd){
	struct PACKET *hp = (struct PACKET *)malloc(sizeof (struct PACKET));
	int bytes, sent;

	hp->flag = OK;
	hp->commid = CD;
	strcpy(hp->data, cmd->path);
	hp->len = strlen(hp->data);

	np = htonp(hp);

	if((sent = send(sfd, np, size_packet, 0)) < 0){
		fprintf(stderr, "Error sending packets !!\n");
		return;
	}

	if((bytes = recv(sfd, np, size_packet, 0)) <= 0){
		fprintf(stderr, "Error receiving packet..!!\n");
		return;
	}

	hp = ntohp(np);
	printf("Current Server directory :\n%s\n", hp->data);
}

//GET Operation........................................................................................
void server_get(struct command *cmd, int sfd){

	FILE *out = fopen(cmd->fileName, "wb");
	if(!out){
		fprintf(stderr, "Error creating file. See if required permissions are satisfied !!\n");
		return;
	}

	struct PACKET *hp = (struct PACKET *)malloc(sizeof (struct PACKET));
	int sent, bytes;

	hp->commid = GET;
	hp->flag = OK;
	strcpy(hp->data, cmd->path);
	hp->len = strlen(hp->data);

	np = htonp(hp);


	if((sent = send(sfd, np, size_packet, 0)) < 0){
		fprintf(stderr, "Error sending packets !!\n");
		return;
	}

	recvFile(hp, np, sfd, out);
	fclose(out);
}

//PUT Operation.........................................................................................
void server_put(struct command *cmd, int sfd){

	FILE *in = fopen(cmd->path, "rb");
	if(!in){
		fprintf(stderr, "Error opening file. See if pemissions are satisfied !!\n");
		return;
	}

	struct PACKET *hp = (struct PACKET *)malloc(sizeof (struct PACKET));
	int sent, bytes;

	hp->commid = PUT;
	hp->flag = OK;
	strcpy(hp->data, cmd->fileName);
	hp->len = strlen(hp->data);

	np = htonp(hp);	

	if((sent = send(sfd, np, size_packet, 0)) <= 0){
		fprintf(stderr, "Error sending packets !!\n");
		return;
	}

	if((bytes = recv(sfd, np, size_packet, 0)) <= 0){
		fprintf(stderr, "Error receiving confirmation packets !!\n");
		return;
	}	

	hp = ntohp(np);
	//strcpy(hp->data, cmd->fileName)
	if(hp->flag == READY){
		sendFile(hp, sfd, in);
	}else{
		fprintf(stderr, "Error creating file at server !!\n");
	}
	fclose(in);
}


//Parsing user Input...............................................................................
struct command * getUserCommand(char *input){
	char n[DATALEN+2];
	strcpy(n, "");
	strcat(n, " ");
	strcat(n, input);
	strcat(n, " ");
	char *option, *last;
	struct command *cmd = (struct command *)malloc(sizeof (struct command));
	strcpy(cmd->path, "");
	strcpy(cmd->fileName, "");
	cmd->id = -2;
	option = strtok(input, " \t");
	if(!strcmp(option, "!pwd")){
		cmd->id = CPWD;
	}

	else if(!strcmp(option, "!ls")){
		cmd->id = CLS;
	}

	else if(!strcmp(option, "!cd")){
		cmd->id = CCD;
		option = strtok(NULL, " ");
		strcpy(cmd->path, option);
	}

	else if(!strcmp(option, "pwd")){
		cmd->id = PWD;
	}

	else if(!strcmp(option, "ls")){
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
		fprintf(stderr,"Incorrect command. Refer included README.txt with application !!\n\n");
	
	return cmd;
}

/*void client_QUIT(struct command *cmd, int sfd){
	struct PACKET *hp = (struct PACKET *)malloc(sizeof(struct PACKET));

	hp->flag = QUIT;
	hp->commid = QUIT;
	strcpy(hp->data, "");
	hp->len = 0;

	np = htonp(hp);

	int sent = send(sfd, np, size_packet, 0);

} */