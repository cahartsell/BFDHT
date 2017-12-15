//
// Created by charles on 11/9/17.
//

#include <iostream>
#include <cstring>
#include <zmq.h>

#include "Node.h"

/* Local helper function declarations -- definitions at end of file */
void print_digest(digest_t digest);
int checkConsensus(value_t* responses, int responseCnt, value_t* answer);
int checkEntryConsensus(table_entry_t* responses, int responseCnt, table_entry_t* answer);
int findReadyWorker(worker_t** in_worker, std::vector<worker_t> &workers);
int findWorkerWithKey(worker_t** in_worker, std::vector<worker_t> &workers, digest_t &key);
int handleWorkerMsg(zmq::message_t &msg, zmq::socket_t *pubSock, zmq::socket_t *clientSock, worker_t *worker, char* myTopic);
int handleNetworkMsg(zmq::message_t &msg, zmq::socket_t *clientSock, std::vector<worker_t> &workers);
int handleClientMsg(zmq::message_t &msg, zmq::socket_t *pubSock, std::vector<worker_t> &workers);

Node::Node()
{
    /* Init Chord and Create Node ID */
    chord = new Chord();
    //chord->getNodeID();
    chord->getNodeIP();


    /*Turn std::string representation of IP into
     * 4-char array. Don't judge me.*/
    std::string tempString = chord->getIP() + ".";

    char* token;
    char strarray[100];
    for(int i = 0; i < tempString.length(); i++) {
        strarray[i] = tempString[i];
    }

    token = std::strtok(strarray,".");
    this->myTopic[0] = (char)atoi(token);
    for (int i = 1;i < MSG_TOPIC_SIZE; i++) {
        token = std::strtok(NULL,".");
        this->myTopic[i] = (char)atoi(token);
    }

    //verify that the 4 char array is good
//    for (int i = 0; i < MSG_TOPIC_SIZE; i++) {
//        std::cout << (int)this->myTopic[i];
//    }
//    std::cout << std::endl;

    /* Prepare ZMQ Context and main sockets */
    zmqContext = new zmq::context_t();
    subSock = new zmq::socket_t(*zmqContext, ZMQ_SUB);
    pubSock = new zmq::socket_t(*zmqContext, ZMQ_PUB);

    /* Node runs in seperate thread. Local client needs socket to send in requests */
    clientSockNode = new zmq::socket_t(*zmqContext, ZMQ_PAIR);
    clientSockClient = new zmq::socket_t(*zmqContext, ZMQ_PAIR);

    /* Connect sockets to multicast address */
    /* FIXME: Does this belong in startup? */
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
    subSock->setsockopt(ZMQ_SUBSCRIBE, myTopic, strlen(DEFAULT_TOPIC));

    //update finger table
}

Node::~Node()
{
    /* ZMQ Context class destructor calls zmq_ctx_destroy */
    delete zmqContext;
    delete subSock, pubSock;
    delete chord;

    freeTableMem();

    /* TODO: Cleanup worker threads here */
}

int Node::startup() {
    /* FIXME: Need to confirm pthread_create success */

    /* Setup client to node sockets */
    std::string tempAddr;
    tempAddr = "ipc://";
    tempAddr += IPC_PATH;
    tempAddr += "client";
    clientSockNode->bind(tempAddr.c_str());
    clientSockClient->connect(tempAddr.c_str());

    /* Spawn worker thread pool */
    int i;
    worker_t temp_worker;
    worker_arg_t* temp_args;
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

    /* Spawn thread for Node::main() */
    /* NOTE: This is done last to ensure all worker sockets have been created */
    /* FIXME: Make this (and everything else) thread safe. */
    pthread_create(&mainThread, NULL, main, (void*) this);

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

    zmq::message_t msg;
    zmq::socket_t *tempSock;
    char *data, *cpyStart, *cpyEnd, *msgStart;
    worker_t* tempWorker;
    size_t workerMsgSize;
    int result, running, i;

    /* Setup polling for all worker sockets, client socket, and primary pub/sub socket */
    zmq::pollitem_t pollItems[POLL_IDS_SIZE];
    for(i = 0; i < INIT_WORKER_THREAD_CNT; i++) {
        pollItems[i].socket = *(context->workers[i].sock);
        pollItems[i].events = ZMQ_POLLIN;
    }
    pollItems[NETWORK_SUB].socket = *(context->subSock);
    pollItems[NETWORK_SUB].events = ZMQ_POLLIN;
    pollItems[CLIENT_PAIR].socket = *(context->clientSockNode);
    pollItems[CLIENT_PAIR].events = ZMQ_POLLIN;

#ifdef NODE_DEBUG
    std::cout << "Node main function called. Listening for messages" << std::endl;
#endif

    running = true;
    while(running) {
        /* Block until any pollItems socket can be read */
        result = zmq_poll(pollItems, POLL_IDS_SIZE, -1);

#ifdef NODE_DEBUG
        std::cout << "Node main function received message. Poll returned: " << result << " errno: " << errno << std::endl;
#endif

        /* Check worker requests */
        /* FIXME: Check for <0 error case */
        for (i = 0; i < INIT_WORKER_THREAD_CNT; i++){
            if (pollItems[i].revents > 0) {
                tempSock = (zmq::socket_t*)(&(pollItems[i].socket));
                tempSock->recv(&msg);

                /* TODO: Handle worker message here */
                handleWorkerMsg(msg, context->pubSock, context->clientSockNode, &(context->workers[i]), context->myTopic);
            }
        }

        /* Check messages from other nodes in network */
        if (pollItems[NETWORK_SUB].revents > 0){
            tempSock = (zmq::socket_t*)(&(pollItems[NETWORK_SUB].socket));
            tempSock->recv(&msg);

            /* TODO: Handle message from another node */
            handleNetworkMsg(msg, context->clientSockNode, context->workers);
        }

        /* Check client requests */
        if (pollItems[CLIENT_PAIR].revents > 0){
            tempSock = (zmq::socket_t*)(&(pollItems[CLIENT_PAIR].socket));
            tempSock->recv(&msg);

            /* TODO: Handle client request */

            worker_msg_header_t tempHeader;
            memcpy(&tempHeader, msg.data(), sizeof(tempHeader));
            if (tempHeader.msgType == MSG_TYPE_THREAD_SHUTDOWN){
                /* TODO: Shutdown threads here */
                running = false;
            }
            else{
                handleClientMsg(msg, context->pubSock, context->workers);
            }
        }

/*
#ifdef NODE_DEBUG
        msg_header_t msgHeader;
        char msgTopic[MSG_TOPIC_SIZE + 1];
        memcpy(msgTopic, msgHeader.msgTopic, MSG_TOPIC_SIZE);
        msgTopic[MSG_TOPIC_SIZE] = '\0';
        std::cout << "Node received message. Type: " << msgHeader.msgType;
        std::cout << "\t Topic: " << msgHeader.msgTopic  << "\tSize: " << recvMsg.size() << std::endl;
#endif

        // Strip topic from message
        workerMsgSize = recvMsg.size() - MSG_TOPIC_SIZE;
        zmq::message_t sendMsg(workerMsgSize);
        // FIXME: Need checks on memory sizes before copying
        cpyStart = static_cast<char*>(recvMsg.data()) + MSG_TOPIC_SIZE;
        cpyEnd = static_cast<char*>(recvMsg.data()) + recvMsg.size();
        msgStart = static_cast<char*>(sendMsg.data());
        std::copy(cpyStart, cpyEnd, msgStart);
*/

    }
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

            case MSG_TYPE_PRE_PREPARE: {
#ifdef NODE_DEBUG
                std::cout << "Handling pre-prepare message" << std::endl;
#endif
                size_t dataSize = msg.size() - sizeof(worker_pre_prepare_t);
                worker_pre_prepare_t *ppMsg = (worker_pre_prepare_t *) malloc(msg.size());
                if (ppMsg == nullptr) {
                    /* FIXME: Handle error condition */
                    break;
                }
                memcpy(ppMsg, msg.data(), msg.size());
                context->localPut(ppMsg->digest, ppMsg->data, dataSize);
                break;

            }

            case MSG_TYPE_GET_DATA_FWD: {
#ifdef NODE_DEBUG
                std::cout << "Handling get_fwd message" << std::endl;
#endif
                if (msg.size() < sizeof(worker_get_fwd_msg_t)) {
                    /* FIXME: Handle error condition */
                    break;
                }

                worker_get_fwd_msg_t *getMsg = (worker_get_fwd_msg_t *) malloc(msg.size());
                if (getMsg == nullptr) {
                    /* FIXME: Handle error condition */
                    break;
                }
                memcpy(getMsg, msg.data(), msg.size());
                /* localGet returns pointer to data in hash table and its size */
                int dataSize;
                void *data;
                context->localGet(getMsg->digest, &data, &dataSize);

                size_t repSize = sizeof(worker_get_rep_msg_t) + dataSize;
                worker_get_rep_msg_t *repMsg = (worker_get_rep_msg_t *) malloc(repSize);
                memcpy(repMsg->msgTopic,getMsg->sender,MSG_TOPIC_SIZE);
                repMsg->msgType = MSG_TYPE_GET_DATA_REP;
                repMsg->digest = getMsg->digest;
                memcpy(repMsg->data,data,dataSize);

                zmq::message_t tempMsg(repSize);
                memcpy(tempMsg.data(), repMsg, repSize);
                sock->send(tempMsg);

                free(repMsg);
                free(data);
                break;
            }

            case MSG_TYPE_PUT_DATA_REQ: {
#ifdef NODE_DEBUG
                std::cout << "Worker put request started. MSG Size: " << msg.size() << std::endl;
                std::cout << "\t\t Struct Size: " << sizeof(worker_put_req_msg_t) << std::endl;
#endif
                if (msg.size() < sizeof(worker_put_req_msg_t)) {
                    /* FIXME: Handle error condition */
                    break;
                }

                worker_put_req_msg_t *putMsg = (worker_put_req_msg_t *) malloc(msg.size());
                if (putMsg == nullptr) {
                    /* FIXME: Handle error condition */
                    break;
                }
                memcpy(putMsg, msg.data(), msg.size());

#ifdef NODE_DEBUG
                std::cout << "Worker (" << id << ") starting pre-prepare: " << *((int*)putMsg->data) << std::endl;
#endif

                char targetTopic[MSG_TOPIC_SIZE];
                char tempTopic[MSG_TOPIC_SIZE];
                memcpy(targetTopic,context->myTopic,3);
                targetTopic[3] = (char)((((int)putMsg->digest.bytes[CryptoPP::SHA256::DIGESTSIZE-1])%NUM_NODES)+1);
                memcpy(tempTopic,targetTopic,4);
                size_t ppSize = msg.size() + 3*MSG_TOPIC_SIZE;
                worker_pre_prepare_t *prePrepareMsg = (worker_pre_prepare_t *) malloc(ppSize);
                prePrepareMsg->msgType = MSG_TYPE_PRE_PREPARE;
                size_t dataSize = msg.size() - sizeof(worker_put_req_msg_t);

                int peerCnt = 0;
                int targetIp;
                for (int i = 0; i < DHT_REPLICATION; i++) {
#ifdef NODE_DEBUG
                    std::cout << "Creating a pre-prepare message" << std::endl;
#endif
                    targetIp = (int)targetTopic[3] + i;
                    if (targetIp > NUM_NODES) {targetIp -= NUM_NODES;}
                    tempTopic[3] = (char)targetIp;
                    memcpy(prePrepareMsg->msgTopic,tempTopic,4);
                    for (int j = 0; j < DHT_REPLICATION; j++) {
                        if (i == j) continue;
                        targetIp = (int)targetTopic[3] + j;
                        if (targetIp > NUM_NODES) {targetIp -= NUM_NODES;}
                        tempTopic[3] = (char)targetIp;
                        memcpy(prePrepareMsg->peers[peerCnt],tempTopic,4);
                        peerCnt++;
                    }
                    peerCnt = 0;
                    prePrepareMsg->digest = putMsg->digest;
                    mempcpy(prePrepareMsg->data, putMsg->data, dataSize);
                    zmq::message_t tempMsg(ppSize);
                    memcpy(tempMsg.data(), prePrepareMsg, ppSize);
                    sock->send(tempMsg);
                }

                //context->localPut(putMsg->digest, putMsg->data, dataSize);

                /* localPut blocks until message is stored in hash table. */
                /* Safe to free memory at this point */
                free(putMsg);
                free(prePrepareMsg);
                break;
            }

            /* FIXME: Shouldn't get message always be fixed size? */
            case MSG_TYPE_GET_DATA_REQ: {
#ifdef NODE_DEBUG
                std::cout << "Worker get request started. MSG Size: " << msg.size() << std::endl;
                std::cout << "\t\t Struct Size: " << sizeof(worker_get_req_msg_t) << std::endl;
#endif
                if (msg.size() < sizeof(worker_get_req_msg_t)) {
                    /* FIXME: Handle error condition */
                    break;
                }

                worker_get_req_msg_t *getMsg = (worker_get_req_msg_t *) malloc(msg.size());
                if (getMsg == nullptr) {
                    /* FIXME: Handle error condition */
                    break;
                }
                memcpy(getMsg, msg.data(), msg.size());

                char targetTopic[MSG_TOPIC_SIZE];
                char tempTopic[MSG_TOPIC_SIZE];
                memcpy(targetTopic,context->myTopic,3);
                targetTopic[3] = (char)((((int)getMsg->digest.bytes[CryptoPP::SHA256::DIGESTSIZE-1])%NUM_NODES)+1);
                memcpy(tempTopic,targetTopic,4);
                size_t gfSize = sizeof(worker_get_fwd_msg_t);
                worker_get_fwd_msg_t *getFwdMsg = (worker_get_fwd_msg_t *) malloc(gfSize);
                getFwdMsg->msgType = MSG_TYPE_GET_DATA_FWD;
                getFwdMsg->digest = getMsg->digest;


                int targetIp;
                for (int i = 0; i < DHT_REPLICATION; i++) {
#ifdef NODE_DEBUG
                    std::cout << "Creating a get message" << std::endl;
#endif
                    targetIp = (int)targetTopic[3] + i;
                    if (targetIp > NUM_NODES) {targetIp -= NUM_NODES;}
                    tempTopic[3] = (char)targetIp;
                    memcpy(getFwdMsg->msgTopic,tempTopic,4);

                    zmq::message_t tempMsg(gfSize);
                    memcpy(tempMsg.data(), getFwdMsg, gfSize);
                    sock->send(tempMsg);
                }



                /* TODO: Send data back to client here */

                /* localGet blocks until message is retrieved from hash table. */
                /* Safe to free memory at this point */
                free(getMsg);
                free(getFwdMsg);
                break;
            }

            default:
                /* FIXME: Unrecognized message error */
                break;
        } /* End switch MSG_TYPE */

        worker_msg_header_t finishedMsg;
        zmq::message_t fMsg(sizeof(finishedMsg));
        finishedMsg.msgType = MSG_TYPE_WORKER_FINISHED;
        memcpy(fMsg.data(), &finishedMsg, sizeof(finishedMsg));
        sock->send(fMsg);
    }
}

int Node::put(std::string key_str, void* data_ptr, int data_bytes)
{
    /* Find who to route this request too. Currently assumes all requests are local */
    digest_t digest;
    int result, msgSize;
    /* Get hash digest of key string */
    result = computeDigest(key_str, &digest);
    if (result != 0) {
#ifdef NODE_DEBUG
        std::cout << "ERROR: Node::put failed to generate hash digest from key: " << key_str << std::endl;
#endif
        return -1;
    }

    msgSize = sizeof(worker_put_req_msg_t) + data_bytes;
    worker_put_req_msg_t *putMsg;
    putMsg = static_cast<worker_put_req_msg_t*>(malloc(msgSize));
    if (putMsg == nullptr){
#ifdef NODE_DEBUG
        std::cout << "ERROR: Node::put failed to allocate memory" << std::endl;
#endif
        return -1;
    }
    putMsg->msgType = MSG_TYPE_PUT_DATA_REQ;
    putMsg->digest = digest;

    memcpy(putMsg->msgTopic, DEFAULT_TOPIC, sizeof(putMsg->msgTopic));
    memcpy(putMsg->sender, myTopic, sizeof(putMsg->sender)); //FIXME: MY IP
    memcpy(putMsg->data, data_ptr, data_bytes);

    zmq::message_t msg(msgSize);
    memcpy(msg.data(), putMsg, msg.size());
    this->clientSockClient->send(msg);

    return 0;
}

int Node::get(std::string key_str, void** data_ptr, int* data_bytes)
{
    if (data_bytes == nullptr){
        std::cout << "ERROR: Node::get received null pointer for data_bytes arg" << std::endl;
        return -1;
    }

    /* Get hash digest of key string */
    digest_t digest;
    int result;
    result = computeDigest(key_str, &digest);
    if (result != 0) {
        std::cout << "ERROR: Node::get failed to generate hash digest from key: " << key_str << std::endl;
        return -1;
    }

    /* Construct message */
    worker_get_req_msg_t getMsg;
    getMsg.msgType = MSG_TYPE_GET_DATA_REQ;
    getMsg.digest = digest;
    memcpy(getMsg.msgTopic, DEFAULT_TOPIC, MSG_TOPIC_SIZE);
    memcpy(getMsg.sender, myTopic, MSG_TOPIC_SIZE);

    /* Send message to node main thread */
    zmq::message_t sendMsg(sizeof(getMsg)), recvMsg;
    memcpy(sendMsg.data(), &getMsg, sendMsg.size());
    this->clientSockClient->send(sendMsg);

    /* Wait for responses from node main thread */
    worker_get_rep_msg_t *getRep;
    value_t responses[DHT_REPLICATION];
    int num_responses = 0;
    zmq::pollitem_t pollSock[1];
    pollSock[0].socket = this->clientSockClient;
    pollSock[0].events = ZMQ_POLLIN;
    /* FIXME: need to have timeout */
    while(num_responses < DHT_REPLICATION) {
#ifdef NODE_DEBUG
        std::cout << "Node::get waiting for message." << std::endl;
#endif
        /* Poll on socket and check for timeout */
        result = zmq_poll(pollSock, 1, DEFAULT_TIMEOUT_MS);
        if(pollSock[0].revents <= 0){
            std::cout << "Node::get timeout while waiting for message." << std::endl;
            if (num_responses >= 3){
                break;
            }
            else{
                for (int i = 0; i < num_responses; i++){
                    free(responses[i].value_ptr);
                }
                return -1;
            }
        }
        this->clientSockClient->recv(&recvMsg);
        getRep = static_cast<worker_get_rep_msg_t *>(recvMsg.data());

        if (getRep->digest == digest) {
            responses[num_responses].value_size = recvMsg.size() - sizeof(worker_get_rep_msg_t);
            responses[num_responses].value_ptr = malloc(responses[num_responses].value_size);
            if(responses[num_responses].value_ptr == nullptr){
                std::cout << "ERROR: Node::get failed to allocate memory for responses." << std::endl;
                for (int i = 0; i < num_responses; i++){
                    free(responses[i].value_ptr);
                }
                return -1;
            }
            memcpy(responses[num_responses].value_ptr, getRep->data, responses[num_responses].value_size);
        }
        else{
            /* FIXME: How to best handle this? */
            //std::cout << "ERROR: Node::get received response with incorrect hash key." << std::endl;
            //return -1;
        }
        num_responses++;
    }

    value_t answer;
    result = checkConsensus(responses, DHT_REPLICATION, &answer);
    int tempInt;
    for (int i = 0; i < DHT_REPLICATION; i++){
        tempInt = *((int*)(responses[i].value_ptr));
        std::cout << "Response " << i << ": " << tempInt << std::endl;
        free(responses[i].value_ptr);
    }
    if (result != 0){
        /* TODO: Handle case with no consensus */
        std::cout << "ERROR: Node::get failed to reach a consensus." << std::endl;
        free(answer.value_ptr);
        return -1;
    }
    if (answer.value_size <= 0){
        /* TODO: Handle case */
        std::cout << "ERROR: Node::get received invalid answer data size." << std::endl;
        free(answer.value_ptr);
        return -1;
    }
    if (answer.value_ptr == nullptr){
        /* TODO: Handle case */
        std::cout << "ERROR: Node::get received null answer pointer." << std::endl;
        free(answer.value_ptr);
        return -1;
    }

    /* Copy data to memory block and return */
    *(data_ptr) = malloc(answer.value_size);
    if (*(data_ptr) == nullptr) {
        std::cout << "ERROR: Node::get failed to allocate memory for data." << std::endl;
        free(answer.value_ptr);
        return -1;
    }
    memcpy(*(data_ptr), answer.value_ptr, answer.value_size);
    *(data_bytes) = answer.value_size;
    free(answer.value_ptr);

    return 0;
}

int Node::localPut(digest_t digest, void* data_ptr, int data_bytes)
{
    /* Input checks */
    if (data_ptr == nullptr){
        std::cout << "ERROR: Node::localPut received NULL data pointer." << std::endl;
        return -1;
    }
    if (data_bytes == 0){
        std::cout << "ERROR: Node::localPut received data with size 0." << std::endl;
        return -1;
    }
    if (data_bytes > MAX_DATA_SIZE){
        std::cout << "ERROR: Node::localPut received data over maximum size. Bytes: " << data_bytes << std::endl;
        return -1;
    }

    /* Store value in hash table */
    value_t value;
    /* Allocate memory then copy data to storage */
    value.value_ptr = malloc((size_t)data_bytes);
    value.value_size = data_bytes;
    if (value.value_ptr == nullptr){
        std::cout << "ERROR: Node::localPut failed to allocate " << value.value_size << " bytes of memory." << std::endl;
        return -1;
    }
    memcpy(value.value_ptr, data_ptr, (size_t)value.value_size);

    /* Add/Update value in table */
    this->tableMutex.lock();
    this->table[digest] = value;
    this->tableMutex.unlock();

    return 0;
}

int Node::localGet(digest_t digest, void** data_ptr, int* data_bytes)
{
    value_t out_value;

    /* Check input */
    if (data_bytes == nullptr){
        std::cout << "ERROR: Node::localGet received NULL output size pointer." << std::endl;
        return -1;
    }

    /* Retrieve stored value and place into outputs */
    this->tableMutex.lock();
    out_value = this->table[digest];
    if (out_value.value_size == 0){
        std::cout << "ERROR: Node::localGet table lookup returned empty data value." << std::endl;
        return -1;
    }
    if (out_value.value_ptr == nullptr){
        std::cout << "ERROR: Node::localGet table lookup returned null data pointer." << std::endl;
        return -1;
    }
    if (out_value.value_size > MAX_DATA_SIZE){
        std::cout << "ERROR: Node::localGet table lookup returned data over maximum size. Bytes: " << out_value.value_size << std::endl;
        return -1;
    }
    *data_bytes = out_value.value_size;
    *data_ptr = malloc(*data_bytes);
    if(*data_ptr == nullptr){
        std::cout << "ERROR: Node::localGet failed to allocate memory." << std::endl;
        this->tableMutex.unlock();
        return -1;
    }
    memcpy(*data_ptr, out_value.value_ptr, *data_bytes);
    this->tableMutex.unlock();

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

int checkConsensus(value_t* responses, int responseCnt, value_t* answer)
{
    /* FIXME: Generalize for >4 */
    /* FIXME: Check for null response data_ptr */
    int agreementCnt = 1;
    int tempSize = responses[0].value_size;
    void *tempPtr = responses[0].value_ptr;
    for (int i = 1; i < 4; i++){
        if (tempSize == responses[i].value_size) {
            if (memcmp(tempPtr, responses[i].value_ptr, tempSize) == 0) {
                agreementCnt++;
            }
        }
    }
    if (agreementCnt >= 3){
        answer->value_size = tempSize;
        answer->value_ptr = malloc(tempSize);
        if(answer->value_ptr == nullptr){
            std::cout << "ERROR: checkConsensus failed to allocate memory" << std::endl;
            return -1;
        }
        memcpy(answer->value_ptr, tempPtr, tempSize);
    }
    else {
        agreementCnt = 1;
        tempSize = responses[1].value_size;
        tempPtr = responses[1].value_ptr;
        if (tempSize == responses[0].value_size) {
            if (memcmp(tempPtr, responses[0].value_ptr, tempSize) == 0) {
                agreementCnt++;
            }
        }
        for (int i = 2; i < 4; i++) {
            if (tempSize == responses[i].value_size) {
                if (memcmp(tempPtr, responses[i].value_ptr, tempSize) == 0) {
                    agreementCnt++;
                }
            }
        }
        if (agreementCnt >= 3) {
            answer->value_size = tempSize;
            answer->value_ptr = malloc(tempSize);
            if(answer->value_ptr == nullptr){
                std::cout << "ERROR: checkConsensus failed to allocate memory" << std::endl;
                return -1;
            }
            memcpy(answer->value_ptr, tempPtr, tempSize);
        } else {
            /* No agreement on data size */
            return -1;
        }
    }

    return 0;
}

int checkEntryConsensus(table_entry_t* responses, int responseCnt, table_entry_t* answer)
{
    /* FIXME: Generalize for >4 */
    int agreementCnt = 1;
    digest_t tempDigest = responses[0].digest;
    int tempSize = responses[0].data_size;
    void *tempPtr = responses[0].data_ptr;
    for (int i = 1; i < 4; i++){
        if(tempDigest == responses[i].digest) {
            if (tempSize == responses[i].data_size) {
                if (memcmp(tempPtr, responses[i].data_ptr, tempSize) == 0) {
                    agreementCnt++;
                }
            }
        }
    }
    if (agreementCnt >= 3){
        answer->data_size = tempSize;
        answer->data_ptr = malloc(tempSize);
        if(answer->data_ptr == nullptr){
            std::cout << "ERROR: checkConsensus failed to allocate memory" << std::endl;
            return -1;
        }
        memcpy(answer->data_ptr, tempPtr, tempSize);
    }
    else {
        agreementCnt = 1;
        tempDigest = responses[0].digest;
        tempSize = responses[0].data_size;
        tempPtr = responses[0].data_ptr;
        if(tempDigest == responses[0].digest) {
            if (tempSize == responses[0].data_size) {
                if (memcmp(tempPtr, responses[0].data_ptr, tempSize) == 0) {
                    agreementCnt++;
                }
            }
        }
        for (int i = 2; i < 4; i++) {
            if(tempDigest == responses[i].digest) {
                if (tempSize == responses[i].data_size) {
                    if (memcmp(tempPtr, responses[i].data_ptr, tempSize) == 0) {
                        agreementCnt++;
                    }
                }
            }
        }
        if (agreementCnt >= 3) {
            answer->data_size = tempSize;
            answer->data_ptr = malloc(tempSize);
            if(answer->data_ptr == nullptr){
                std::cout << "ERROR: checkConsensus failed to allocate memory" << std::endl;
                return -1;
            }
            memcpy(answer->data_ptr, tempPtr, tempSize);
        } else {
            /* No agreement on data size */
            return -1;
        }
    }

    return 0;
}

/* Searches the workers vector (private member of Node) to find a non-busy worker thread
 * Sets argument pointer to first available worker
 * Returns 0 on success or -1 otherwise */
int findReadyWorker(worker_t** in_worker, std::vector<worker_t> &workers)
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

/* Searches the workers vector (private member of Node) to find a worker thread
 * that is currently working on a specified key.
 * Sets argument in_worker to desired worker if found. Otherwise, does not modify.
 * Returns 0 on success or -1 otherwise */
int findWorkerWithKey(worker_t** in_worker, std::vector<worker_t> &workers, digest_t &key)
{
    /* Input check */
    if (in_worker == nullptr){
        return -1;
    }

    std::vector<worker_t>::iterator it;
    worker_t* tempWorker;
    for(it = workers.begin(); it != workers.end(); it++){
        tempWorker = it.base();
        if ((tempWorker->busy > 0) && (tempWorker->currentKey == key)){
            *in_worker = tempWorker;
            return 0;
        }
    }

    /* No worker currently using specified key */
    return -1;
}

int handleWorkerMsg(zmq::message_t &msg, zmq::socket_t *pubSock, zmq::socket_t *clientSock, worker_t *worker, char* myTopic){
    zmq::message_t outMsg(msg.size());
    worker_msg_header_t *msgHeader, *outMsgHeader;
    msgHeader = static_cast<worker_msg_header_t*>(msg.data());
    outMsgHeader = static_cast<worker_msg_header_t*>(outMsg.data());

    switch (msgHeader->msgType){
        case MSG_TYPE_PUT_DATA_REP:
            /* TODO: Does the worker need to reply to put requests? */
            break;

        case MSG_TYPE_GET_DATA_REP:
#ifdef NODE_DEBUG
            std::cout << "Worker handler received GET_DATA reply." << std::endl;
#endif
            /* Revise sender and Publish message to other nodes */
            memcpy(outMsg.data(), msg.data(), outMsg.size());
            memcpy(outMsgHeader->sender, myTopic, MSG_TOPIC_SIZE);
            if (memcmp(outMsgHeader->msgTopic, myTopic, MSG_TOPIC_SIZE) == 0) {
                std::cout << "sending to self." << std::endl;
                clientSock->send(outMsg);
            }
            else {
                std::cout << "sending to network." << std::endl;
                pubSock->send(outMsg);
            }
            break;

        /* Forward message to network unless it is directed to this node */
        case MSG_TYPE_PRE_PREPARE:
        case MSG_TYPE_GET_DATA_FWD:
        case MSG_TYPE_PREPARE:
        case MSG_TYPE_COMMIT:
            memcpy(outMsg.data(), msg.data(), outMsg.size());
            memcpy(outMsgHeader->sender, myTopic, MSG_TOPIC_SIZE);
            if (memcmp(outMsgHeader->msgTopic, myTopic, MSG_TOPIC_SIZE) == 0) {
                worker->sock->send(outMsg);
            }
            else {
                pubSock->send(outMsg);
            }

        case MSG_TYPE_WORKER_FINISHED:
            worker->busy = false;
            break;


        default:
            /* TODO: Handle unknown msg type case */
            break;
    }

    return 0;
}

int handleNetworkMsg(zmq::message_t &msg, zmq::socket_t *clientSock, std::vector<worker_t> &workers)
{
    msg_header_t msgHeader;
    memcpy(&msgHeader, msg.data(), sizeof(msgHeader));

    worker_get_req_msg_t *getReq;
    worker_t *tempWorker;
    int result;

    switch (msgHeader.msgType){
        /* Local node needs to do some work. send to worker thread */
        case MSG_TYPE_GET_DATA_REQ:
        case MSG_TYPE_PRE_PREPARE:
        case MSG_TYPE_PUT_DATA_REQ:
        case MSG_TYPE_GET_DATA_FWD:
            getReq = static_cast<worker_get_req_msg_t*>(msg.data());
            /* Dispatch message to worker */
            /* FIXME: Can't assume worker will always be available */
            result = findReadyWorker(&tempWorker, workers);
            if(result == 0) {
                tempWorker->sock->send(msg);
                tempWorker->busy = true;
                tempWorker->currentKey = getReq->digest;
            }
            else{
#ifdef NODE_DEBUG
                std::cout << "All worker threads busy." << std::endl;
#endif
                /* TODO: Handle no available worker */
            }
            tempWorker = nullptr;
            getReq = nullptr;
            break;

        case MSG_TYPE_PREPARE:
        case MSG_TYPE_COMMIT:
            /* Find who this message should be sent to */
            getReq = static_cast<worker_get_req_msg_t*>(msg.data());
            result = findWorkerWithKey(&tempWorker, workers, getReq->digest);
            if(result == 0) {
                tempWorker->sock->send(msg);
            }
            else{
#ifdef NODE_DEBUG
                std::cout << "Received PBFT message, but failed to find worker with key." << std::endl;
#endif
                /* TODO: Handle no worker with key */
            }
            tempWorker = nullptr;
            getReq = nullptr;
            break;

        case MSG_TYPE_GET_DATA_REP:
#ifdef NODE_DEBUG
            std::cout << "Network handler received GET_DATA reply." << std::endl;
#endif
            clientSock->send(msg);
            break;

        default:
            /* TODO: Unrecognized message type error */
            break;
    }

    return 0;
}

int handleClientMsg(zmq::message_t &msg, zmq::socket_t *pubSock, std::vector<worker_t> &workers)
{
    msg_header_t msgHeader;
    memcpy(&msgHeader, msg.data(), sizeof(msgHeader));
    /* FIXME: This msg is from client. May need to do something differently */

    switch (msgHeader.msgType){
        /* Local node needs to do some work. send to worker thread */
        case MSG_TYPE_GET_DATA_REQ:
        case MSG_TYPE_PUT_DATA_REQ:
            /* Dispatch message to worker */
            /* FIXME: Can't assume worker will always be available */
            worker_t *tempWorker;
            int result;
            result = findReadyWorker(&tempWorker, workers);
            if(result == 0) {
                tempWorker->sock->send(msg);
            }
            else{
#ifdef NODE_DEBUG
                std::cout << "All worker threads busy." << std::endl;
#endif
                /* TODO: Handle no available worker */
            }
            break;

        default:
            break;
    }

    return 0;
}