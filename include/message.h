//
// Created by charles on 12/6/17.
//

#ifndef BFDHT_MESSAGES_H
#define BFDHT_MESSAGES_H

#include <cstdint>
#include <string>
#include <zmq.hpp>

#include "types.h"

#define MSG_TOPIC_SIZE 4
#define IP_ADDR_SIZE 4

/* Message Types */
#define MSG_TYPE_THREAD_SHUTDOWN    0x01
#define MSG_TYPE_PUT_DATA_REQ       0x02
#define MSG_TYPE_GET_DATA_REQ       0x03
#define MSG_TYPE_PUT_DATA_REP       0x04
#define MSG_TYPE_GET_DATA_REP       0x05
#define MSG_TYPE_PRE_PREPARE        0x06
#define MSG_TYPE_PREPARE            0x07
#define MSG_TYPE_WORKER_FINISHED    0x08
#define MSG_TYPE_GET_DATA_FWD       0x09
#define MSG_TYPE_COMMIT             0x0A

typedef struct msg_header_t{
    uint16_t msgType;
    char data[];
} msg_header_t;

typedef struct worker_msg_header_t{
    worker_msg_header_t() : msgType((uint16_t)0) {}
    uint16_t msgType;
} worker_msg_header_t;

typedef struct worker_put_req_msg_t {
    worker_put_req_msg_t() : msgType(MSG_TYPE_PUT_DATA_REQ) {}
    uint16_t msgType;
    digest_t digest;
    char data[];
} worker_put_req_msg_t;

typedef struct worker_put_rep_msg_t {
    worker_put_rep_msg_t() : msgType(MSG_TYPE_PUT_DATA_REP) {}
    uint16_t msgType;
    digest_t digest;
    char result;

    /* Define comparison operator for worker_put_rep_msg_t */
    bool operator== (const worker_put_rep_msg_t& other) const {
        if (this->result != other.result) {
            return false; /* Results don't match */
        }
        if (!(this->digest == other.digest)){
            return false; /* Digests are different */
        }
        return true; /* Structs match */
    }
} worker_put_rep_msg_t;

typedef struct worker_pre_prepare_t {
    worker_pre_prepare_t() : msgType(MSG_TYPE_PRE_PREPARE) {}
    uint16_t msgType;
    digest_t digest;
    char peers[3][IP_ADDR_SIZE];
    char data[];
} worker_pre_prepare_t;

typedef struct worker_get_req_msg_t {
    worker_get_req_msg_t() : msgType(MSG_TYPE_GET_DATA_REQ) {}
    uint16_t msgType;
    digest_t digest;
} worker_get_req_msg_t;

typedef struct worker_get_fwd_msg_t {
    worker_get_fwd_msg_t() : msgType(MSG_TYPE_GET_DATA_FWD) {}
    uint16_t msgType;
    digest_t digest;
} worker_get_fwd_msg_t;

typedef struct worker_get_rep_msg_t {
    worker_get_rep_msg_t() : msgType(MSG_TYPE_GET_DATA_REP) {}
    uint16_t msgType;
    digest_t digest;
    char data[];
} worker_get_rep_msg_t;

typedef struct worker_prepare_t {
    worker_prepare_t() : msgType(MSG_TYPE_PREPARE) {}
    uint16_t msgType;
    digest_t digest;
    char data[];
} worker_prepare_t;

typedef struct worker_commit_t {
    worker_commit_t() : msgType(MSG_TYPE_COMMIT) {}
    uint16_t msgType;
    digest_t digest;
    char data[];
} worker_commit_t;

#endif //BFDHT_MESSAGES_H
