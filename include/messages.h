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
#define MSG_TYPE_PUT_DATA_REQ       0x02
#define MSG_TYPE_GET_DATA_REQ       0x03
#define MSG_TYPE_PUT_DATA_REP       0x04
#define MSG_TYPE_GET_DATA_REP       0x05
#define MSG_TYPE_CHORD              0x06

typedef struct msg_header_t{
    char msgTopic[MSG_TOPIC_SIZE];
    uint16_t msgType;
    char data[];
} msg_header_t;

typedef struct worker_msg_header_t{
    worker_msg_header_t() : msgType((uint16_t)0) {}
    uint16_t msgType;
} worker_msg_header_t;

typedef struct worker_put_msg_t {
    worker_put_msg_t() : msgType(MSG_TYPE_PUT_DATA_REQ) {}
    uint16_t msgType;
    digest_t digest;
    char data[];
} worker_put_msg_t;

typedef struct worker_get_msg_t {
    worker_get_msg_t() : msgType(MSG_TYPE_GET_DATA_REQ) {}
    uint16_t msgType;
    digest_t digest;
} worker_get_msg_t;

#endif //BFDHT_MESSAGES_H
