#include "commons.h"

#define U_INPUTLEN 1024		//Largo maximo de input

//Struct command es un struct que contiene el id de comando, path para si se usa el ls o cd
// y el filename para el get y put
struct command{
	int id;
	char path[DATALEN];
	char fileName[DATALEN];
};

size_t size_sockaddr = sizeof(struct sockaddr), size_packet = sizeof(struct PACKET);

struct command * parseInput(char *);


void server_pwd(int);
void server_ls(int);
void server_cd(struct command *, int);
void server_get(struct command *, int);
void server_put(struct command *, int);
//void client_QUIT(struct command *, int);