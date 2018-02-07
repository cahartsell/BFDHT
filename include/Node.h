//
// Created by charles on 11/9/17.
//

#ifndef BFDHT_NODE_H
#define BFDHT_NODE_H

#include <zmq.hpp>
#include <vector>
#include <pthread.h>
#include <string>
#include <mutex>

#include "types.h"
#include "Chord.h"
#include "message.h"

/********** DEBUGGING FLAG ON ***********/
#define NODE_DEBUG

#define MAX_KEY_LEN                 256     // 256 character key length
#define MAX_DATA_SIZE               1024    // 1 kB max data size
#define MAX_MSG_SIZE                MAX_DATA_SIZE + 256
#define INIT_WORKER_THREAD_CNT      10

#define DHT_REPLICATION 4
#define NUM_NODES 4

/* Hardcoded multicast IP Address */
#define MULTICAST_IP "239.192.1.1"
#define NETWORK_PROTOCOL "tcp://"
#define PORT "8476"
#define DEFAULT_TIMEOUT_MS 2000

/* Path for IPC communication.
 * '@' symbol makes this an unnamed socket path (doesn't exist on disk)
 * Don't have to worry about socket cleanup or already exists conflicts */
#define IPC_BASE_PATH "@/tmp/BFDHT/"
#define MAX_PATH_LEN 104

/* Hardcoded message topic until more appropriate topics available */
#define DEFAULT_TOPIC "ABCD"

enum pollIds{
    WORKER_0 = 0,
    WORKER_1 = 1,
    WORKER_2 = 2,
    WORKER_3 = 3,
    WORKER_4 = 4,
    WORKER_5 = 5,
    WORKER_6 = 6,
    WORKER_7 = 7,
    WORKER_8 = 8,
    WORKER_9 = 9,
    NETWORK_SRV = 10,
    CLIENT_CMD = 11,
    POLL_IDS_SIZE = 12
};


/* Data type for elements stored in the Hash Table */
typedef struct value_t{
    value_t() : value_ptr(nullptr), value_size(0) {}    /* Constructor & default values */
    void* value_ptr;                                    /* Pointer to start of value data */
    int value_size;                                     /* Size of data stored at value_ptr in bytes */
} value_t;

typedef struct table_entry_t {
    void* data_ptr;
    size_t data_size;
    digest_t digest;
} table_entry_t;

/* ZMQ identity storage type */
typedef struct zmq_id_t{
    size_t size;
    char* id_ptr;
} zmq_id_t;

/* Data type passed to main thread */
typedef struct main_thread_data_t {
    void *node;
    pthread_barrier_t* barrier;
} main_thread_data_t;

/* Data type passed to worker thread */
typedef struct worker_arg_t{
    worker_arg_t() : node(nullptr), id(0) {}
    void *node;
    int id;
} worker_arg_t;

/* Data type for maintaining worker threads */
typedef struct worker_t{
    worker_t() : sock(-1) {}  /* Constructor */
    int sock;
    digest_t currentKey;
} worker_t;

/* Worker request socket type */
typedef struct worker_req_sock_t{
    zmq::socket_t *sock;
    std::string curEndpoint;
} worker_req_sock_t;

typedef struct worker_new_job_msg_t{
    sockaddr_storage reqAddr;
    socklen_t addrLen;
};

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

    /* Public Variables */


private:
    /* Private Functions */
    static void* main(void* arg);
    static void* workerMain(void* arg);
    int computeDigest(std::string key_str, digest_t* digest);
    void freeTableMem();
    int localPut(digest_t digest, void* data_ptr, int data_bytes);
    int localGet(digest_t digest, void** data_ptr, int* data_bytes);
    void setMyTopic(std::string ip);


    /* Private Variables */
    CryptoPP::SHA256 hash;
    std::map<digest_t, value_t> table;
    std::mutex tableMutex;
    Chord* chord;
    zmq::context_t *zmqContext;
    zmq::socket_t *clientSock;
    pthread_t mainThread, workerThreads[INIT_WORKER_THREAD_CNT];
    char myTopic[MSG_TOPIC_SIZE];
};


#endif //BFDHT_NODE_H
