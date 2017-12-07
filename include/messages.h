//
// Created by charles on 12/6/17.
//

#ifndef BFDHT_MESSAGES_H
#define BFDHT_MESSAGES_H

#include <cstdint>
#include <string>

#define MSG_TOPIC_SIZE 4

/* Message Types */
#define MSG_TYPE_THREAD_SHUTDOWN    0x01
#define MSG_TYPE_PUT_DATA           0x02
#define MSG_TYPE_GET_DATA           0x03

/* Expected MINIMUM message sizes */
#define WORKER_PUT_DATA_MSG_SIZE    sizeof(worker_msg_header_t) + sizeof(digest_t) + 1
#define WORKER_GET_DATA_MSG_SIZE    sizeof(worker_msg_header_t) + sizeof(digest_t)

typedef struct msg_header_t{
    msg_header_t() : msgType((uint16_t)0) {}
    char msgTopic[MSG_TOPIC_SIZE];
    uint16_t msgType;
} msg_header_t;

typedef struct worker_msg_header_t{
    worker_msg_header_t() : msgType((uint16_t)0) {}
    uint16_t msgType;
} worker_msg_header_t;

typedef struct worker_put_msg_t {
    uint16_t msgType;
    digest_t digest;
    char data[];
} worker_put_msg_t;

typedef struct worker_get_msg_t {
    uint16_t msgType;
    digest_t digest;
} worker_get_msg_t;

#endif //BFDHT_MESSAGES_H
