//
// Created by charles on 11/9/17.
//

#ifndef BFDHT_NODE_H
#define BFDHT_NODE_H

#include <zmq.hpp>
#include <vector>
#include <pthread.h>
#include <string>

#include "types.h"
#include "Chord.h"
#include "messages.h"

/********** DEBUGGING FLAG ON ***********/
#define NODE_DEBUG

#define MAX_KEY_LEN                 256     // 256 character key length
#define MAX_DATA_SIZE               1024    // 1 kB max data size
#define INIT_WORKER_THREAD_CNT      10

#define DHT_REPLICATION 4

/* Hardcoded multicast IP Address */
#define MULTICAST_IP "239.192.1.1"
#define PORT "8476"

/* Path for IPC communication.
 * '@' symbol makes this an unnamed socket path (doesn't exist on disk)
 * Don't have to worry about socket cleanup or already exists conflicts */
#define IPC_PATH "@/tmp/BFDHT/"

/* Hardcoded message topic until more appropriate topics available */
#define DEFAULT_TOPIC "ABCD"


/* Data type for elements stored in the Hash Table */
typedef struct value_t{
    value_t() : value_ptr(nullptr), value_size(0) {}    /* Constructor & default values */
    void* value_ptr;                                    /* Pointer to start of value data */
    int value_size;                                     /* Size of data stored at value_ptr in bytes */
} value_t;

/* Data type passed to worker thread */
typedef struct worker_arg_t{
    worker_arg_t() : node(nullptr), id(0) {}
    void *node;
    int id;
} worker_arg_t;

/* Data type for maintaining worker threads */
typedef struct worker_t{
    worker_t() : thread(0), sock(0), busy(0) {}  /* Constructor */
    pthread_t thread;
    zmq::socket_t *sock;
    int busy;
} worker_t;

class Node
{
public:
    /* Public Functions */
    Node();
    ~Node();
    int startup();
    int shutdown();
    int put(std::string key_str, void* data_ptr, int data_bytes);
    int get(std::string key_str, void** data_ptr, int* data_bytes);

    int send(std::string topic, void* data_ptr, size_t data_size);
    int computeDigest(std::string key_str, digest_t* digest);

    /* Public Variables */


private:
    /* Private Functions */
    static void* main(void* arg);
    static void* workerMain(void* arg);
    int findReadyWorker(worker_t** worker);
    //int computeDigest(std::string key_str, digest_t* digest);
    void freeTableMem();
    int localPut(digest_t digest, void* data_ptr, int data_bytes);
    int localGet(digest_t digest, void** data_ptr, int* data_bytes);
    void setMyTopic(std::string ip);

    /* Private Variables */
    CryptoPP::SHA256 hash;
    std::map<digest_t, value_t> table;
    Chord* chord;
    zmq::context_t *zmqContext;
    zmq::socket_t *subSock, *pubSock;
    pthread_t mainThread;
    std::vector<worker_t> workers;
    char myTopic[MSG_TOPIC_SIZE];


};


#endif //BFDHT_NODE_H
