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
typedef struct rocket_ctrl_pkt {
    uint8_t type;               /* control packet type */
    uint16_t cid;                /* connection identifier */
    uint32_t k;                 /* shared private key */
    /* TODO: other parameters ... */
} rocket_ctrl_pkt;
void rocket_ltobytes(uint32_t l, char *bytes);
void rocket_itobytes(uint16_t i, char *bytes);
void rocket_stobytes(uint8_t s, char *bytes);
uint32_t rocket_bytestol(char *bytes);
uint16_t rocket_bytestoi(char *bytes);
uint8_t rocket_bytestos(char *bytes);
char *rocket_serialize_ctrlpkt(rocket_ctrl_pkt *pkt);
rocket_ctrl_pkt *rocket_deserialize_ctrlpkt(char *bytes);

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
