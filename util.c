#include "rocket.h"

#include <strings.h>

/* convert 32bit unsigned integer to big-endian byte array */
void rocket_ltobytes(uint32_t l, unsigned char *bytes) {
    bytes[0] = (l >> 24) & 0xFF;
    bytes[1] = (l >> 16) & 0xFF;
    bytes[2] = (l >> 8) & 0xFF;
    bytes[3] = l & 0xFF;
}

/* convert 16bit unsigned integer to big-endian byte array */
void rocket_itobytes(uint16_t i, unsigned char *bytes) {
    bytes[0] = (i >> 8) & 0xFF;
    bytes[1] = i & 0xFF;
}

/* convert 8bit unsigned integer to a single byte */
void rocket_stobytes(uint8_t s, unsigned char *bytes) {
    bytes[0] = s & 0xFF;
}

/* convert big-endian byte array to 32bit unsigned integer */
uint32_t rocket_bytestol(unsigned char *bytes) {
    return (uint32_t)bytes[3] |
        (uint32_t)bytes[2] << 8 |
        (uint32_t)bytes[1] << 16 |
        (uint32_t)bytes[0] << 24;
}

/* convert big-endian byte array to 16bit unsigned integer */
uint16_t rocket_bytestoi(unsigned char *bytes) {
    return (uint16_t)bytes[1] |
        (uint16_t)bytes[0] << 8;
}

/* convert single byte to 8bit unsigned integer */
uint8_t rocket_bytestos(unsigned char *bytes) {
    return (uint8_t)bytes[0];
}

/* transform a struct rocket_ctrl_pkt to a byte array */
void rocket_serialize_ctrlpkt(rocket_ctrl_pkt *pkt, unsigned char *bytes) {
    if (pkt->type == 1) {
        rocket_stobytes(pkt->type, bytes);
        rocket_itobytes(pkt->port, bytes+1);
        rocket_ltobytes(pkt->buffer, bytes+3);
        bzero(bytes+7, ROCK_CTRLPKTSIZE-7);
    }
    else if (pkt->type == 2) {
        rocket_stobytes(pkt->type, bytes);
        BN_bn2bin(pkt->k, bytes+1);
        bzero(bytes+1+ROCK_DH_BYTE, ROCK_CTRLPKTSIZE-1-ROCK_DH_BYTE);
    }
    else if (pkt->type == 3) {
        rocket_stobytes(pkt->type, bytes);
        rocket_itobytes(pkt->port, bytes+1);
        BN_bn2bin(pkt->k, bytes+3);
    }
    else if (pkt->type == 4) {
        rocket_stobytes(pkt->type, bytes);
        rocket_itobytes(pkt->cid, bytes+1);
        rocket_ltobytes(pkt->buffer, bytes+3);
        bzero(bytes+7, ROCK_CTRLPKTSIZE-7);
    }
    else if (pkt->type == 5) {
        rocket_stobytes(pkt->type, bytes);
        rocket_itobytes(pkt->cid, bytes+1);
        rocket_ltobytes(pkt->buffer, bytes+3);
        bzero(bytes+7, ROCK_CTRLPKTSIZE-7);
    }
    else if (pkt->type == 6) {
        rocket_stobytes(pkt->type, bytes);
        BN_bn2bin(pkt->k, bytes+1);
        bzero(bytes+1+ROCK_DH_BYTE, ROCK_CTRLPKTSIZE-1-ROCK_DH_BYTE);
    }
    else if (pkt->type == 7) {
        rocket_stobytes(pkt->type, bytes);
        rocket_itobytes(pkt->cid, bytes+1);
        BN_bn2bin(pkt->k, bytes+3);
    }
    else if (pkt->type == 8) {
        rocket_stobytes(pkt->type, bytes);
        rocket_ltobytes(pkt->buffer, bytes+1);
        bzero(bytes+5, ROCK_CTRLPKTSIZE-5);
    }
    else if (pkt->type == 100 || pkt->type == 200) {
        rocket_stobytes(pkt->type, bytes);
        bzero(bytes+1, ROCK_CTRLPKTSIZE-1);
    }
    else if (pkt->type == 50) {
        rocket_stobytes(pkt->type, bytes);
        rocket_itobytes(pkt->cid, bytes+1);
        bzero(bytes+3, ROCK_CTRLPKTSIZE-3);
    }
}

/* transform a byte array to a struct rocket_ctrl_pkt */
rocket_ctrl_pkt *rocket_deserialize_ctrlpkt(unsigned char *bytes) {
    rocket_ctrl_pkt *pkt = malloc(sizeof(rocket_ctrl_pkt));
    pkt->k = BN_new();
    pkt->type = rocket_bytestos(bytes);
    if (pkt->type == 1) {
        pkt->port = rocket_bytestoi(bytes+1);
        pkt->buffer = rocket_bytestol(bytes+3);
    }
    else if (pkt->type == 2) {
        BN_bin2bn(bytes+1, ROCK_DH_BYTE, pkt->k);
    }
    else if (pkt->type == 3) {
        pkt->port = rocket_bytestoi(bytes+1);
        BN_bin2bn(bytes+3, ROCK_DH_BYTE, pkt->k);
    }
    else if (pkt->type == 4) {
        pkt->cid = rocket_bytestoi(bytes+1);
        pkt->buffer = rocket_bytestol(bytes+3);
    }
    else if (pkt->type == 5) {
        pkt->cid = rocket_bytestoi(bytes+1);
        pkt->buffer = rocket_bytestol(bytes+3);
    }
    else if (pkt->type == 6) {
        BN_bin2bn(bytes+1, ROCK_DH_BYTE, pkt->k);
    }
    else if (pkt->type == 7) {
        pkt->cid = rocket_bytestoi(bytes+1);
        BN_bin2bn(bytes+3, ROCK_DH_BYTE, pkt->k);
    }
    else if (pkt->type == 8) {
        pkt->buffer = rocket_bytestol(bytes+1);
    }
    else if (pkt->type == 100 || pkt->type == 200) {
        
    }
    else if (pkt->type == 50) {
        pkt->cid = rocket_bytestoi(bytes+1);
    }
    return pkt;
}

int rocket_list_insert(rocket_list_node **head, rocket_t *rocket, uint16_t cid) {
    rocket_list_node *node;
    if ((node = (rocket_list_node *)malloc(sizeof(*node))) == NULL)
        return -1;
    node->cid = cid;
    node->rocket = rocket;
    if (*head == 0) {
        *head = node;
        node->next = 0;
    } else {
        node->next = *head;
        *head = node;
    }
    return 0;
}

int rocket_list_remove(rocket_list_node **head, uint16_t cid) {
    if (*head == 0)
        return -1;
    rocket_list_node *node;
    rocket_list_node *previous;
    node = *head;
    previous = 0;
    while (1) {
        if (node->cid == cid) {
            if (previous == 0 && node->next == 0) {
                *head = 0;
                free(node);
            }
            else if (previous == 0 && node->next != 0) {
                *head = node->next;
                free(node);
            }
            else if (previous != 0 && node->next == 0) {
                previous->next = 0;
                free(node);
            }
            else if (previous != 0 && node->next != 0) {
                previous->next = node->next;
                free(node);
            }
            return 0;
        } else if (node->next == 0) {
            return -1;
        } else {
            previous = node;
            node = node->next;
        }
    }
}

rocket_t *rocket_list_find(rocket_list_node *head, uint16_t cid) {
    if (head == 0)
        return 0;
    rocket_list_node *node;
    node = head;
    while (1) {
        if (node->cid == cid)
            return node->rocket;
        else if (node->next == 0)
            return 0;
        else
            node = node->next;
    }
}

rocket_t *rocket_list_findbyport(rocket_list_node *head, uint16_t port) {
    if (head == 0)
        return 0;
    rocket_list_node *node;
    node = head;
    while (1) {
        if (node->rocket->port == port)
            return node->rocket;
        else if (node->next == 0)
            return 0;
        else
            node = node->next;
    }
}

/* calculate the (positive) difference between two unsigned
 * integer stored as: number % 4.294.967.295 (UINT32_MAX).
 * this function cannot hold differences greater than that
 * number. 
 * difference = a - b */
uint32_t rocket_mod_diff(uint32_t a, uint32_t b) {
    if (a > b) {
        return a - b;
    }
    else if (a < b) {
        return a + (UINT32_MAX - b);
    }
    else {
        return 0;
    }
}

void rocket_list_print(rocket_list_node *head) {
    if (head == 0)
        return;
    rocket_list_node *node;
    node = head;
    while (node != 0) {
        rocket_list_print_item(node->rocket);
        node = node->next;
    }
}

void rocket_list_print_item(rocket_t *rocket) {
    printf("----- rocket -----\n");
    printf("cid:\t\t%d\n", rocket->cid);
    printf("role:\t\t%d\n", (int)rocket->role);
    printf("state:\t\t%d\n", (int)rocket->state);
    printf("sd:\t\t%d\n", rocket->sd);
    printf("port:\t\t%d\n", rocket->port);
    printf("tcp_task:\t%d\n", rocket->tcp_task);
    printf("a:\t\t%s\n", BN_bn2hex(rocket->a));
    printf("k:\t\t%s\n", BN_bn2hex(rocket->k));
    printf("lasthbtime:\t%u\n", rocket->lasthbtime);
    printf("sentcounter:\t%d\n", rocket->sentcounter);
    printf("rcvdcounter:\t%d\n", rocket->rcvdcounter);
    printf("tosendbytes:\t%d\n", rocket->tosendbytes);
    printf("------------------\n");
}
