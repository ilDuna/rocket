#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <pthread.h>
#include <openssl/bn.h>

/* rocket API configuration */
#define ROCK_UDPPORT        211
#define ROCK_CTRLPKTSIZE    ROCK_DH_BYTE+3
#define ROCK_CTRLMAXRETRY   5
#define ROCK_CTRLTIMEOUT    5
#define ROCK_DH_BIT         128
#define ROCK_DH_BYTE        ROCK_DH_BIT/8
#define ROCK_TCP_RCVBUF     64000
#define ROCK_TCP_SNDBUF     64000
#define ROCK_HB_RATE        15
#define ROCK_NET_CHECK      30
#define ROCK_HB_MAXLOSS     4
#define ROCK_SNDRCV_REFR    3

/* error codes */
#define ROCK_ERR_NOTFOUND   (-2)

typedef enum {SERVER, CLIENT} rocket_role;
typedef enum {CONNECTED, AVAILABLE, SUSPENDED, CLOSED} rocket_state;
typedef struct rocket_t {
    rocket_role role;       /* server or client rocket */
    uint16_t cid;           /* connection identifier */
    BIGNUM* a;              /* private key */
    BIGNUM* k;              /* shared private key */
    BIGNUM* challenge;      /* challenge-response for reconnection */
    rocket_state state;     /* current state */
    uint32_t sd;            /* tcp socket descriptor */
    uint16_t port;          /* tcp port */
    uint8_t tcp_task;       /* 1 if the tcp re/connection thread
                            has been started, 0 otherwise */
    uint32_t buffer_size;   /* inflight buffer size */
    uint32_t dlvdbytes;     /* bytes delivered (!= sent) within
                            last rocket_send before rocket suspension */
    uint32_t rcvdbytes;     /* bytes received within last rocket_recv */
    pthread_t cnet_monitor; /* client network monitor */
    uint32_t lasthbtime;    /* last valid heartbeat timestamp in s */
    char* serveraddr;       /* server ip address */
} rocket_t;
typedef struct rocket_list_node {
    uint16_t cid;
    rocket_t* rocket;
    struct rocket_list_node* next;
} rocket_list_node;
typedef struct rocket_ctrl_pkt {
    uint8_t type;           /* control packet type */
    uint16_t port;          /* requested tcp port */
    uint16_t cid;           /* connection identifier */
    BIGNUM* k;              /* shared private key */
    uint32_t buffer;        /* tcp receive buffer size */
} rocket_ctrl_pkt;

/* rocket API */
int rocket_ctrl_server(rocket_list_node **head, pthread_mutex_t *lock);
uint16_t rocket_server(rocket_list_node **head, uint16_t port, pthread_mutex_t *lock);
uint16_t rocket_client(rocket_list_node **head, char *addr, uint16_t port, pthread_mutex_t *lock);
int rocket_connect(int reconnect, rocket_list_node **head, char *addr, uint16_t port, pthread_mutex_t *lock);
//int rocket_close(rocket_list_node **head, uint16_t cid, pthread_mutex_t *lock);
int rocket_send(rocket_list_node **head, uint16_t cid, char *buffer, uint32_t length, pthread_mutex_t *lock);
int rocket_recv(rocket_list_node **head, uint16_t cid, char **buffer, pthread_mutex_t *lock);

/* rocket list */
int rocket_list_insert(rocket_list_node **head, rocket_t *rocket, uint16_t cid);
int rocket_list_remove(rocket_list_node **head, uint16_t cid);
rocket_t *rocket_list_find(rocket_list_node *head, uint16_t cid);
rocket_t *rocket_list_findbyport(rocket_list_node *head, uint16_t port);
/* control serialization & deserialization */
void rocket_ltobytes(uint32_t l, unsigned char *bytes);
void rocket_itobytes(uint16_t i, unsigned char *bytes);
void rocket_stobytes(uint8_t s, unsigned char *bytes);
uint32_t rocket_bytestol(unsigned char *bytes);
uint16_t rocket_bytestoi(unsigned char *bytes);
uint8_t rocket_bytestos(unsigned char *bytes);
void rocket_serialize_ctrlpkt(rocket_ctrl_pkt *pkt, unsigned char *bytes);
rocket_ctrl_pkt *rocket_deserialize_ctrlpkt(unsigned char *bytes);
/* debug tools */
void rocket_list_print(rocket_list_node *head);
void rocket_list_print_item(rocket_t *rocket);
