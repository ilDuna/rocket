#include <stdlib.h>
#include <stdio.h>

#define ROCK_UDPPORT      211

typedef enum {SERVER, CLIENT} rocket_role;
typedef enum {CONNECTED, SUSPENDED, CLOSED} rocket_state;

typedef struct rocket_t {
    rocket_role role;       /* server or client rocket */
    int cid;                /* connection identifier */
    long k;                 /* shared private key */
    rocket_state state;     /* current state */
    int n;                  /* number of tcp sockets */
    int* sd;                /* tcp socket descriptors list */
    int* port;              /* tcp ports list */
} rocket_t;
typedef struct rocket_list_node {
    int cid;
    rocket_t* rocket;
    struct rocket_list_node* next;
} rocket_list_node;

int rocket_ctrl_server();
int rocket_ctrl_client();
rocket_t rocket_new(rocket_role role, int n, int *port);
void rocket_close(int cid);

/* send & receive calls should be designed to send the entire msg,
 * so no length argument is necessary? TO-BE-DECIDED. */
void rocket_send(int cid, int port, void *buffer, size_t length);
void rocket_receive(int cid, int port, void *buffer, size_t length);

int rocket_list_insert(rocket_list_node **head, rocket_t *rocket, int cid);
int rocket_list_remove(rocket_list_node **head, int cid);
rocket_t *rocket_list_find(rocket_list_node *head, int cid);
