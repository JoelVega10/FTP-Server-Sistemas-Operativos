#include "commons.h"
#define BACKLOG 20

void recieve_clients();

void start_server();

void precreate_threads();

int check_threads_available();

void attend_client(void* arg);

void out_of_threads();

void client_get(struct PACKET *, int client);

void client_put(struct PACKET *, int client);

void client_ls(struct PACKET *, int client);
