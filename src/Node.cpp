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
    temp += chord->getIP();
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

    /* TODO: Cleanup threads here */
}

int Node::startup() {
    /* FIXME: Need to confirm pthread_create success */
    /* Spawn thread for Node::main() */
    pthread_create(&mainThread, NULL, main, (void*) this);

    /* Spawn worker thread pool */
    int i;
    worker_t temp_worker;
    worker_arg_t* temp_args;
    std::string tempAddr;
    for(i = 0; i < INIT_WORKER_THREAD_CNT; i++){
        /* Setup worker socket using IPC */
        temp_worker.sock = new zmq::socket_t(*zmqContext, ZMQ_PAIR);
        tempAddr = "ipc://";
        tempAddr += IPC_PATH;
        tempAddr += std::to_string(i);
        temp_worker.sock->bind(tempAddr.c_str());

        /* Spawn worker thread */
        temp_args = static_cast<worker_arg_t*>( malloc(sizeof(worker_arg_t)) );
        temp_args->id = i;
        temp_args->node = this;
        temp_worker.busy = false;
        workers.push_back(temp_worker);
        /* FIXME: Need to confirm pthread_create success */
        pthread_create(&(workers[i].thread), NULL, workerMain, (void*) temp_args);
    }

    return 0;
}

int Node::shutdown()
{
    /* TODO: Write this function */
    return 0;
}

void* Node::main(void* arg)
{
    Node* context = static_cast<Node*>(arg);

    zmq::message_t recvMsg;
    msg_header_t msgHeader;
    char msgTopic[MSG_TOPIC_SIZE + 1];
    char *data, *cpyStart, *cpyEnd, *msgStart;
    worker_t* tempWorker;
    size_t workerMsgSize;
    int result, running;

#ifdef NODE_DEBUG
    std::cout << "Node main function called. Listening for messages" << std::endl;
#endif

    running = true;
    while(running) {
        context->subSock->recv(&recvMsg);
        memcpy(&msgHeader, recvMsg.data(), sizeof(msgHeader));

        /* High tech message handler */
        memcpy(msgTopic, msgHeader.msgTopic, MSG_TOPIC_SIZE);
        msgTopic[MSG_TOPIC_SIZE] = '\0';

#ifdef NODE_DEBUG
        std::cout << "Node received message. Type: " << msgHeader.msgType;
        std::cout << "\t Topic: " << msgHeader.msgTopic  << "\tSize: " << recvMsg.size() << std::endl;
#endif

        if (msgHeader.msgType == MSG_TYPE_THREAD_SHUTDOWN){
            /* TODO: Shutdown threads here */
            running = false;
        }

        /* Strip topic from message */
        workerMsgSize = recvMsg.size() - MSG_TOPIC_SIZE;
        zmq::message_t sendMsg(workerMsgSize);
        /* FIXME: Need checks on memory sizes before copying */
        cpyStart = static_cast<char*>(recvMsg.data()) + MSG_TOPIC_SIZE;
        cpyEnd = static_cast<char*>(recvMsg.data()) + recvMsg.size();
        msgStart = static_cast<char*>(sendMsg.data());
        std::copy(cpyStart, cpyEnd, msgStart);

        /* Dispatch message to worker */
        /* FIXME: Can't assume worker will always be available */
        result = context->findReadyWorker(&tempWorker);
        if(result == 0) {
            tempWorker->sock->send(sendMsg);
        }
        else{
#ifdef NODE_DEBUG
            std::cout << "All worker threads busy." << std::endl;
#endif
            /* TODO: Handle no available worker */
        }
    }

    return 0;
}

void* Node::workerMain(void* arg)
{
    /* Copy arg data then free arg pointer */
    worker_arg_t* args = static_cast<worker_arg_t*>(arg);
    Node* context = static_cast<Node*>(args->node);
    int id = args->id;
    free(arg);

    zmq::socket_t *sock;
    std::string tempAddr;
    zmq::message_t msg;
    worker_msg_header_t msgHeader;

    /* Connect socket to main thread */
    sock = new zmq::socket_t(*(context->zmqContext), ZMQ_PAIR);
    tempAddr = "ipc://";
    tempAddr += IPC_PATH;
    tempAddr += std::to_string(id);
    sock->connect(tempAddr.c_str());

#ifdef NODE_DEBUG
    std::cout << "Worker started with id " << id << ". Listening for messages" << std::endl;
#endif

    int running = true;
    while(running) {
#ifdef NODE_DEBUG
        std::cout << "Worker listening for message." << std::endl;
#endif
        sock->recv(&msg);
        memcpy(&msgHeader, msg.data(), sizeof(msgHeader));

#ifdef NODE_DEBUG
        std::cout << "Worker received message. Type: " << msgHeader.msgType << std::endl;
#endif

        /* NOTE: some of the case statements are given their own scope {}
         *       this is to allow different message type variables to be declared
         *       depending on the type of message received */
        switch (msgHeader.msgType) {

            case MSG_TYPE_THREAD_SHUTDOWN:
#ifdef NODE_DEBUG
                std::cout << "Worker shutting down" << std::endl;
#endif
                running = false;
                /* FIXME: Do any cleanup here */
                break;

            case MSG_TYPE_PUT_DATA_REQ: {
#ifdef NODE_DEBUG
                std::cout << "Worker put request started. MSG Size: " << msg.size() << std::endl;
                std::cout << "\t\t Struct Size: " << sizeof(worker_put_msg_t) << std::endl;
#endif
                if (msg.size() < sizeof(worker_put_msg_t)) {
                    /* FIXME: Handle error condition */
                    break;
                }

                worker_put_msg_t *putMsg = (worker_put_msg_t *) malloc(msg.size());
                    if (putMsg == nullptr) {
                    /* FIXME: Handle error condition */
                    break;
                }
                memcpy(putMsg, msg.data(), msg.size());

#ifdef NODE_DEBUG
                std::cout << "Worker (" << id << ") putting value: " << *((int*)putMsg->data) << std::endl;
#endif


                for(int i = 0; i < DHT_REPLICATION; i++){

                }
                size_t dataSize = msg.size() - sizeof(worker_put_msg_t);
                context->localPut(putMsg->digest, putMsg->data, dataSize);

                /* localPut blocks until message is stored in hash table. */
                /* Safe to free memory at this point */
                free(putMsg);
                break;
            }

            /* FIXME: Shouldn't get message always be fixed size? */
            case MSG_TYPE_GET_DATA_REQ: {
#ifdef NODE_DEBUG
                std::cout << "Worker get request started. MSG Size: " << msg.size() << std::endl;
                std::cout << "\t\t Struct Size: " << sizeof(worker_get_msg_t) << std::endl;
#endif
                if (msg.size() < sizeof(worker_get_msg_t)) {
                    /* FIXME: Handle error condition */
                    break;
                }

                worker_get_msg_t *getMsg = (worker_get_msg_t *) malloc(msg.size());
                if (getMsg == nullptr) {
                    /* FIXME: Handle error condition */
                    break;
                }
                memcpy(getMsg, msg.data(), msg.size());

                /* localGet returns pointer to data in hash table and its size */
                int dataSize;
                void *data;
                context->localGet(getMsg->digest, &data, &dataSize);

#ifdef NODE_DEBUG
                std::cout << "Worker (" << id << ") got value: " << *((int*)data) << std::endl;
#endif

                /* TODO: Send data back to client here */

                /* localGet blocks until message is retrieved from hash table. */
                /* Safe to free memory at this point */
                free(getMsg);
                break;
            }

            default:
                /* FIXME: Unrecognized message error */
                break;
        }
    }
}

/* Searches the workers vector (private member of Node) to find a non-busy worker thread
 * Sets argument pointer to first available worker
 * Returns 0 on success or -1 otherwise */
int Node::findReadyWorker(worker_t** in_worker)
{
    /* Input check */
    if (in_worker == nullptr){
        return -1;
    }

    std::vector<worker_t>::iterator it;
    worker_t* tempWorker;
    for(it = workers.begin(); it != workers.end(); it++){
        tempWorker = it.base();
        if (tempWorker->busy == 0){
            *in_worker = tempWorker;
            return 0;
        }
    }

    /* All workers busy */
    return -1;
}

/* FIXME: This is temporary for debugging. Need real version */
int Node::send(std::string topicStr, void* dataPtr, size_t dataSize)
{
    /* Input checks */
    if (topicStr.size() != MSG_TOPIC_SIZE) {
        return -1;
    }
    if (dataPtr == nullptr){
        return -1;
    }

    size_t msgSize = dataSize + MSG_TOPIC_SIZE;
    zmq::message_t msg(msgSize);
    char *dataStart, *dataEnd, *msgDataStart;

    /* Copy topic and data into msg */
    msgDataStart = static_cast<char*>(msg.data());
    std::copy(topicStr.begin(), topicStr.end(), msgDataStart);
    dataStart = static_cast<char*>(dataPtr);
    dataEnd = dataStart + dataSize;
    msgDataStart += MSG_TOPIC_SIZE;
    std::copy(dataStart, dataEnd, msgDataStart);

#ifdef NODE_DEBUG
    std::cout << "Node sending message." << std::endl;
#endif

    pubSock->send(msg);

    return 0;
}

int Node::put(std::string key_str, void* data_ptr, int data_bytes)
{
    /* Find who to route this request too. Currently assumes all requests are local */
    digest_t digest;
    int result;
    /* Get hash digest of key string */
    result = computeDigest(key_str, &digest);
    if (result != 0) {
#ifdef NODE_DEBUG
        std::cout << "ERROR: Node::put failed to generate hash digest from key: " << key_str << std::endl;
#endif
        return -1;
    }

    localPut(digest, data_ptr, data_bytes);

    return 0;
}

int Node::get(std::string key_str, void** data_ptr, int* data_bytes)
{
    /* Find who to route this request too. Currently assumes all requests are local */
    digest_t digest;
    int result;
    /* Get hash digest of key string */
    result = computeDigest(key_str, &digest);
    if (result != 0) {
        std::cout << "ERROR: Node::put failed to generate hash digest from key: " << key_str << std::endl;
        return -1;
    }

    localGet(digest, data_ptr, data_bytes);

    return 0;
}

/* FIXME: Likely need mutex for localPut and localGet */
int Node::localPut(digest_t digest, void* data_ptr, int data_bytes)
{
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

int Node::localGet(digest_t digest, void** data_ptr, int* data_bytes)
{
    value_t out_value;

    /* Check input */
    if (data_bytes == nullptr){
        std::cout << "ERROR: Node::get received NULL output size pointer." << std::endl;
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
    print_digest(*digest);
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

void Node::setMyTopic(std::string ip) {
    char* token;
    char strarray[100];
    for(int i = 0; i < ip.length(); i++) {
        strarray[i] = ip[i];
    }

    token = std::strtok(strarray,".");
    for (int i = 0;i < 3; i++) {
        token = std::strtok(NULL,".");
        this->myTopic[i] = char
    //std::cout << lastIp << std::endl;
    //this->myId.key.bytes[CryptoPP::SHA256::DIGESTSIZE - 1] = (char)()
    for (int i = 0;i < FINGER_TABLE_SIZE; i++) {
        //this->finger[i]
    }
}

/************** Helper function definitions ***************/
void print_digest(digest_t digest)
{
    int i;

    std::cout << "Calculated digest: " << std::endl;
    for (i=0; i<CryptoPP::SHA256::DIGESTSIZE; i++){
        std::cout << ((char) digest.bytes[i]) << std::endl;
    }
    std::cout << std::endl;
}
