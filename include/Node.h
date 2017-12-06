//
// Created by charles on 11/9/17.
//

#ifndef BFDHT_NODE_H
#define BFDHT_NODE_H

//#define NODE_DEBUG
#define MAX_KEY_LEN 256         // 256 character key length
#define MAX_DATA_SIZE 1024     // 1 kB max data size

#include <zmq.hpp>

#include "types.h"
#include "Chord.h"

/* Hardcoded multicast IP Address. Some sort of ZMQ black magic is going on here */
#define MULTICAST_IP "239.192.1.1"
#define PORT "8476"

/* Hardcoded message topic until more appropriate topics available */
#define DEFAULT_TOPIC "TOPIC_0"


/* Data type for elements stored in the Hash Table */
typedef struct value_t{
    value_t() : value_ptr(nullptr), value_size(0) {}    /* Constructor & default values */
    void* value_ptr;                                    /* Pointer to start of value data */
    int value_size;                                     /* Size of data stored at value_ptr in bytes */
} value_t;

class Node
{
public:
    /* Public Functions */
    Node();
    ~Node();
    int main();
    int put(std::string key_str, void* data_ptr, int data_bytes);
    int get(std::string key_str, void** data_ptr, int* data_bytes);

    int send(std::string &msgStr);

    /* Public Variables */


private:
    /* Private Functions */
    int computeDigest(std::string key_str, digest_t* digest);
    void freeTableMem();

    /* Private Variables */
    CryptoPP::SHA256 hash;
    std::map<digest_t, value_t> table;
    Chord* chord;
    zmq::context_t *zmqContext;
    zmq::socket_t *subSock, *pubSock;

    /*finger table*/

};


#endif //BFDHT_NODE_H
