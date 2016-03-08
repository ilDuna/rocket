#include "rocket.h"

void rocket_ltobytes(uint32_t l, unsigned char *bytes) {
    bytes[0] = (l >> 24) & 0xFF;
    bytes[1] = (l >> 16) & 0xFF;
    bytes[2] = (l >> 8) & 0xFF;
    bytes[3] = l & 0xFF;
}

void rocket_itobytes(uint16_t i, unsigned char *bytes) {
    bytes[0] = (i >> 8) & 0xFF;
    bytes[1] = i & 0xFF;
}

void rocket_stobytes(uint8_t s, unsigned char *bytes) {
    bytes[0] = s & 0xFF;
}

uint32_t rocket_bytestol(unsigned char *bytes) {
    return (uint32_t)bytes[3] |
        (uint32_t)bytes[2] << 8 |
        (uint32_t)bytes[1] << 16 |
        (uint32_t)bytes[0] << 24;
}

uint16_t rocket_bytestoi(unsigned char *bytes) {
    return (uint16_t)bytes[1] |
        (uint16_t)bytes[0] << 8;
}

uint8_t rocket_bytestos(unsigned char *bytes) {
    return (uint8_t)bytes[0];
}

void rocket_serialize_ctrlpkt(rocket_ctrl_pkt *pkt, unsigned char *bytes) {
    rocket_stobytes(pkt->type, bytes);
    rocket_itobytes(pkt->cid, bytes+1);
    rocket_ltobytes(pkt->k, bytes+3);
}

rocket_ctrl_pkt *rocket_deserialize_ctrlpkt(unsigned char *bytes) {
    rocket_ctrl_pkt *pkt = malloc(sizeof(rocket_ctrl_pkt));
    pkt->type = rocket_bytestos(bytes);
    pkt->cid = rocket_bytestoi(bytes+1);
    pkt->k = rocket_bytestol(bytes+3);
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
        return NULL;
    rocket_list_node *node;
    node = head;
    while (1) {
        if (node->cid == cid) {
            return node->rocket;
        }
        else if (node->next == 0) {
            return NULL;
        }
        else {
            node = node->next;
        }
    }
}
