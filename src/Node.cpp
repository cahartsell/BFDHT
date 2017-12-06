//
// Created by charles on 11/9/17.
//

#include <iostream>
#include <cstring>

#include "Node.h"

/* Local helper functions */
void print_digest(digest_t digest);

Node::Node()
{
    /* Init Chord and Create Node ID */
    chord = new Chord();
    //chord->getNodeID();
    chord->getNodeIP();

    /* Prepare ZMQ Context and main sockets */
    zmqContext = new zmq::context_t();
    subSock = new zmq::socket_t(*zmqContext, ZMQ_SUB);
    pubSock = new zmq::socket_t(*zmqContext, ZMQ_PUB);

    /* Connect sockets to multicast address */
    /* FIXME: Interface IP hardcoded because chord cannot discover IP of mininet nodes. */
    std::string temp = "epgm://";
    temp += "10.0.0.1";
    temp += ';';
    temp += MULTICAST_IP;
    temp += ':';
    temp += PORT;
    std::cout << "Node connecting to: " << temp.c_str() << std::endl;
    subSock->connect(temp.c_str());
    pubSock->bind(temp.c_str());

    /* Set subscribed messages - All messages directed to this node */
    /* This will need to be based on IP. All nodes subscribed to DEFAULT_TOPIC */
    subSock->setsockopt(ZMQ_SUBSCRIBE, DEFAULT_TOPIC, strlen(DEFAULT_TOPIC));

    //update finger table
}

Node::~Node()
{
    /* ZMQ Context class destructor calls zmq_ctx_destroy */
    delete zmqContext;
    delete subSock, pubSock;
    delete chord;

    freeTableMem();

    /* FIXME: Cleanup threads here */
}

int Node::startup() {
    /* FIXME: Need to confirm pthread_create success */
    /* Spawn thread for Node::main() */
    pthread_create(&mainThread, NULL, runNode, (void*) this);

    /* Spawn worker thread pool */
    int i;
    worker_t temp_workers[INIT_WORKER_THREAD_CNT];
    std::string tempAddr;
    for(i = 0; i < INIT_WORKER_THREAD_CNT; i++){
        /* Setup worker socket using IPC */
        temp_workers[i].sock = new zmq::socket_t(*zmqContext, ZMQ_PAIR);
        tempAddr = "ipc:///tmp/BFDHT/";
        tempAddr += i;
        temp_workers[i].sock->bind(tempAddr.c_str());

        /* Spawn worker thread */
        temp_workers[i].args.id = i;
        temp_workers[i].args.node = this;
        pthread_create(&(temp_workers[i].thread), NULL, runWorker, (void*) &(temp_workers[i].args));
        workers.push_back(temp_workers[i]);
    }

    return 0;
}

void* Node::runNode(void* arg)
{
    Node* node = (Node*) arg;
    node->main();
}

int Node::main()
{
    zmq::message_t msg;
    char msgData[100];

    std::cout << "Node main function called. Listening for messages" << std::endl;

    /* FIXME: Infinite loop. How do we want to terminate? */
    while(1) {
        subSock->recv(&msg);
        memcpy(msgData, msg.data(), msg.size());
        msgData[msg.size()] = '\0';

        /* High tech message handler */
        std::cout << "Node received message. Contents: " << msgData << std::endl;
    }

    return 0;
}

void* Node::runWorker(void* arg)
{
    worker_arg_t *args = (worker_arg_t*) arg;
    Node *node = (Node*) args->node;
    node->workerMain(args->id);
}

int Node::workerMain(int id)
{
    zmq::socket_t *sock;
    std::string tempAddr;
    zmq::message_t msg;
    char msgData[100];

    /* Connect socket to main thread */
    sock = new zmq::socket_t(*zmqContext, ZMQ_PAIR);
    tempAddr = "ipc:///tmp/BFDHT/";
    tempAddr += id;
    sock->connect(tempAddr.c_str());

    std::cout << "Worker started with id " << id << ". Listening for messages" << std::endl;

    /* FIXME: Infinite loop. How do we want to terminate? */
    while(1) {
        sock->recv(&msg);
        memcpy(msgData, msg.data(), msg.size());
        msgData[msg.size()] = '\0';

        /* High tech message handler */
        std::cout << "Worker received message. Contents: " << msgData << std::endl;
    }

    return 0;
}

/* FIXME: This is temporary for debugging. Need real version */
int Node::send(std::string &msgStr)
{
    zmq::message_t msg(100);

    snprintf((char*) msg.data(), 100, "%s %s", DEFAULT_TOPIC, msgStr.c_str());

    std::cout << "Node sending message: " << (char*) msg.data() << std::endl;

    pubSock->send(msg);

    return 0;
}

int Node::put(std::string key_str, void* data_ptr, int data_bytes)
{
    digest_t digest;
    int result;

    /* Input checks */
    if (data_ptr == nullptr){
        std::cout << "ERROR: Node::put received NULL data pointer." << std::endl;
        return -1;
    }
    if (data_bytes == 0){
        std::cout << "ERROR: Node::put received data with size 0." << std::endl;
        return -1;
    }
    if (data_bytes > MAX_DATA_SIZE){
        std::cout << "ERROR: Node::put received data over maximum size. Bytes: " << data_bytes << std::endl;
        return -1;
    }

    /* Get hash digest of key string */
    result = computeDigest(key_str, &digest);
    if (result != 0) {
        std::cout << "ERROR: Node::put failed to generate hash digest from key: " << key_str << std::endl;
        return -1;
    }

    /* Store value in hash table */
    value_t value;
    /* Allocate memory then copy data to storage */
    value.value_ptr = malloc((size_t)data_bytes);
    value.value_size = data_bytes;
    if (value.value_ptr == nullptr){
        std::cout << "ERROR: Node::put failed to allocate " << value.value_size << " bytes of memory." << std::endl;
        return -1;
    }
    memcpy(value.value_ptr, data_ptr, (size_t)value.value_size);
    /* Add/Update value in table */
    this->table[digest] = value;

    return 0;
}

int Node::get(std::string key_str, void** data_ptr, int* data_bytes)
{
    digest_t digest;
    int result;
    value_t out_value;

    /* Check input */
    if (data_bytes == nullptr || data_ptr == nullptr){
        std::cout << "ERROR: Node::get received NULL output value pointer." << std::endl;
        return -1;
    }

    /* Generate hash digest from key string */
    result = computeDigest(key_str, &digest);
    if (result != 0){
        std::cout << "ERROR: Node::get failed to generate hash digest from key: " << key_str << std::endl;
        return -1;
    }

    /* Retrieve stored value and place into outputs */
    out_value = this->table[digest];
    if (out_value.value_size == 0){
        std::cout << "ERROR: Node::get table lookup returned empty data value." << std::endl;
        return -1;
    }
    if (out_value.value_ptr == nullptr){
        std::cout << "ERROR: Node::get table lookup returned null data pointer." << std::endl;
        return -1;
    }
    if (out_value.value_size > MAX_DATA_SIZE){
        std::cout << "ERROR: Node::get table lookup returned data over maximum size. Bytes: " << out_value.value_size << std::endl;
        return -1;
    }
    *data_ptr = out_value.value_ptr;
    *data_bytes = out_value.value_size;

    return 0;
}

int Node::computeDigest(std::string key_str, digest_t* digest)
{
    /* Convert key std::string to C-string */
    unsigned long key_size = key_str.length() + 1;
    if (key_size > MAX_KEY_LEN){
        return -1;
    }
    auto *key = new char[key_size];
    std::strcpy(key, key_str.c_str());

    /* Use hashing function on value to calculate digest */
    this->hash.CalculateDigest(digest->bytes, (byte*)key, key_size);

#ifdef NODE_DEBUG
    print_digest(digest);
#endif

    return 0;
}

void Node::freeTableMem()
{
    std::map<digest_t, value_t>::iterator it;
    value_t tempVal;

    for(it = this->table.begin(); it != this->table.end(); it++){
        tempVal = it->second;
        free(tempVal.value_ptr);
    }
}

/************** Helper function definitions ***************/
void print_digest(digest_t digest)
{
    int i;

    std::cout << std::endl;
    for (i=0; i<CryptoPP::SHA256::DIGESTSIZE; i++){
        std::cout << ((char) digest.bytes[i]) << std::endl;
    }
    std::cout << std::endl;
}
