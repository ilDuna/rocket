#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <pthread.h>

#define ROCK_UDPPORT        211
#define ROCK_CTRLPKTSIZE    7

typedef enum {SERVER, CLIENT} rocket_role;
typedef enum {CONNECTED, SUSPENDED, CLOSED} rocket_state;
typedef struct rocket_t {
    rocket_role role;       /* server or client rocket */
    uint16_t cid;           /* connection identifier */
    uint32_t k;             /* shared private key */
    rocket_state state;     /* current state */
    uint32_t sd;            /* tcp socket descriptor */
    uint16_t port;          /* tcp port */
} rocket_t;
typedef struct rocket_list_node {
    uint16_t cid;
    rocket_t* rocket;
    struct rocket_list_node* next;
} rocket_list_node;
typedef struct rocket_ctrl_pkt {
    uint8_t type;           /* control packet type */
    uint16_t cid;           /* connection identifier */
    uint32_t k;             /* shared private key */
    /* TODO: other parameters ... */
} rocket_ctrl_pkt;

int rocket_ctrl_server(rocket_list_node **head);
uint16_t rocket_server(rocket_list_node **head, uint16_t port);
uint16_t rocket_client(rocket_list_node **head, uint16_t addr_family, const char *addr, uint16_t port);
int rocket_close(rocket_list_node **head, uint16_t cid);
/* send & receive calls should be designed to send the entire msg,
 * so no length argument is necessary? TO-BE-DECIDED. */
void rocket_send(uint16_t cid, uint16_t port, void *buffer, size_t length);
void rocket_receive(uint16_t cid, uint16_t port, void *buffer, size_t length);

int rocket_list_insert(rocket_list_node **head, rocket_t *rocket, uint16_t cid);
int rocket_list_remove(rocket_list_node **head, uint16_t cid);
rocket_t *rocket_list_find(rocket_list_node *head, uint16_t cid);
void rocket_ltobytes(uint32_t l, unsigned char *bytes);
void rocket_itobytes(uint16_t i, unsigned char *bytes);
void rocket_stobytes(uint8_t s, unsigned char *bytes);
uint32_t rocket_bytestol(unsigned char *bytes);
uint16_t rocket_bytestoi(unsigned char *bytes);
uint8_t rocket_bytestos(unsigned char *bytes);
void rocket_serialize_ctrlpkt(rocket_ctrl_pkt *pkt, unsigned char *bytes);
rocket_ctrl_pkt *rocket_deserialize_ctrlpkt(unsigned char *bytes);
