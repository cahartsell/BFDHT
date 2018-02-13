//
// Created by charles on 11/9/17.
//

#ifndef BFDHT_NODE_H
#define BFDHT_NODE_H

#include <vector>
#include <pthread.h>
#include <string>
#include <mutex>
#include <sys/socket.h>

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
#define CONSENSUS_THRESHOLD 3
#define NUM_NODES 4

#define PORT_STR "8476"
#define PORT 8476
#define DEFAULT_TIMEOUT_MS 2000
#define DEFAULT_WORKER_TIMEOUT_MS 500

/* Path for IPC communication.
 * '@' symbol makes this an unnamed socket path (doesn't exist on disk)
 * Don't have to worry about socket cleanup or already exists conflicts */
#define IPC_BASE_PATH "@/tmp/BFDHT/"
#define MAX_PATH_LEN 104

/* FIXME: I don't particularly like this */
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

/* Data type passed to main thread */
typedef struct manager_thread_data_t {
    pthread_barrier_t* barrier;
    int workerSock[INIT_WORKER_THREAD_CNT];
    pthread_t workerThreads[INIT_WORKER_THREAD_CNT];
    int clientSock;
} manager_thread_data_t;

/* Data type passed to worker thread */
typedef struct worker_arg_t{
    worker_arg_t() : node(nullptr), id(0) {}
    void* node;
    int id;
    int managerSock;
    pthread_barrier_t* barrier;
} worker_arg_t;

/* Data type for maintaining worker threads */
typedef struct worker_t{
    worker_t() : sock(-1) {}  /* Constructor */
    int sock;
    digest_t currentKey;
} worker_t;

typedef struct worker_new_job_msg_t{
    sockaddr_storage reqAddr;
    socklen_t addrLen;
} worker_new_job_msg_t;

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
    static void* manager(void *arg);
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
    pthread_t managerThread;
    char myTopic[MSG_TOPIC_SIZE];
    int clientSock;
};


#endif //BFDHT_NODE_H
