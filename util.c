#include "rocket.h"

int rocket_list_insert(rocket_list_node **head, rocket_t *rocket, int cid) {
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

int rocket_list_remove(rocket_list_node **head, int cid) {
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

rocket_t *rocket_list_find(rocket_list_node *head, int cid) {
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
