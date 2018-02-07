//
// Created by charles on 11/9/17.
//

#include <iostream>
#include <cstring>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>

#include "Node.h"

/* Local helper function declarations -- definitions at end of file */
void print_digest(digest_t digest);
std::string peerArrayToIP(char *ip, size_t ip_len);
int logMsg(std::string msg);
int checkConsensus(value_t* responses, int responseCnt, value_t* answer);
int checkEntryConsensus(table_entry_t* responses, int responseCnt, table_entry_t* answer);
int findWorkerWithKey(worker_t** in_worker, std::vector<worker_t> &workers, digest_t &key);

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
        token = std::strtok(nullptr,".");
        this->myTopic[i] = (char)atoi(token);
    }

    //verify that the 4 char array is good
//    for (int i = 0; i < MSG_TOPIC_SIZE; i++) {
//        std::cout << (int)this->myTopic[i];
//    }
//    std::cout << std::endl;


    /* TODO: update finger table */
}

Node::~Node()
{
    delete chord;

//    std::vector<worker_t>::iterator it;
//    for (it = workers.begin(); it != workers.end(); it++){
//        delete it->sock;
//    }

    freeTableMem();
    /* TODO: Cleanup worker threads here */
}

int Node::startup() {
    /* FIXME: Need to confirm pthread_create success */

    /* Setup worker & Control sockets */
    /* Socket init sections based heavily off Beej's Networking and UNIX IPC Guides */
    /* Available at www.beej.us */
    int tempPair[2], rv, i;
    main_thread_data_t mainArg;
    worker_arg_t workerArg[INIT_WORKER_THREAD_CNT];

    /* Setup worker sockets */
    for(i = 0; i < INIT_WORKER_THREAD_CNT; i++) {
        rv = socketpair(AF_UNIX, SOCK_STREAM, 0, tempPair);
        if (rv < 0){
            perror("Failed to create worker socket pair.");
        }

        mainArg.workerSock[i] = tempPair[0];
        workerArg[i].managerSock = tempPair[1];
    }

    /* Setup client cmd socket */
    rv = socketpair(AF_UNIX, SOCK_STREAM, 0, tempPair);
    if (rv < 0){
        perror("Failed to create CMD socket pair.");
    }

    mainArg.clientSock = tempPair[0];
    this->clientSock = tempPair[1];

    /* Spawn thread for Node::main() */
    /* NOTE: This is done last to ensure all worker sockets have been created */
    /* FIXME: Make this (and everything else) thread safe. */
    pthread_barrier_t mainBarrier;
    rv = pthread_barrier_init(&mainBarrier, nullptr, 2);
    mainArg.node = (void*) this;
    mainArg.barrier = &mainBarrier;
    pthread_create(&mainThread, nullptr, main, (void*) &mainArg);
    pthread_barrier_wait(&mainBarrier);

    /* Spawn worker thread pool */
    pthread_barrier_t workerBarrier;
    rv = pthread_barrier_init(&workerBarrier, nullptr, INIT_WORKER_THREAD_CNT + 1);
    for(i = 0; i < INIT_WORKER_THREAD_CNT; i++){
        /* Spawn worker thread */
        workerArg[i].id = i;
        workerArg[i].node = this;
        workerArg[i].barrier = &workerBarrier;
        /* FIXME: Need to confirm pthread_create success */
        pthread_create(&(workerThreads[i]), nullptr, workerMain, (void*) &(workerArg[i]));
    }
    pthread_barrier_wait(&workerBarrier);

    /* Connect client CMD socket */

    return 0;
}

int Node::shutdown()
{
    /* TODO: Perform other cleanup? */
    /* FIXME: Change this to use proxyControlSock */
    worker_msg_header_t msgHeader;
    zmq::message_t msg(sizeof(msgHeader));
    msgHeader.msgType = MSG_TYPE_THREAD_SHUTDOWN;
    memcpy(msg.data(), &msgHeader, msg.size());
    /* FIXME: Send shutdown message */

    /* FIXME: Wait for shutdown complete message here */
    //zmq::message_t recvMsg;
    //clientSockClient->recv(recvMsg);
    return 0;
}

void* Node::main(void* arg)
{
    auto *mainData = static_cast<main_thread_data_t*>(arg);

    Node *context = static_cast<Node*>(mainData->node);

    pollfd pollItems[POLL_IDS_SIZE];
    int workerSock[INIT_WORKER_THREAD_CNT], i;

    /* Setup worker sockets */
    for(i = 0; i < INIT_WORKER_THREAD_CNT; i++) {
        workerSock[i] = mainData->workerSock[i];
        pollItems[i].fd = workerSock[i];
        pollItems[i].events = POLLIN;
    }

    /* Setup poll for client CMD sock */
    int clientSock = mainData->clientSock;
    pollItems[CLIENT_CMD].fd = clientSock;
    pollItems[CLIENT_CMD].events = POLLIN;

    /* Setup server sock */
    int srvSock, rv;
    addrinfo hints, *srvinfo, *tempinfo;
    std::string tempPath;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    rv = getaddrinfo(nullptr, PORT, &hints, &srvinfo);
    if (rv != 0){
        perror("Getaddrinfo failed.");
    }

    for (tempinfo = srvinfo; tempinfo != nullptr; tempinfo = tempinfo->ai_next){
        srvSock = socket(tempinfo->ai_family, tempinfo->ai_socktype, tempinfo->ai_protocol);
        if (srvSock < 0){
            perror("Socket creation failed");
            continue;
        }

        rv = bind(srvSock, tempinfo->ai_addr, tempinfo->ai_addrlen);
        if (rv < 0){
            perror("Socket bind failed");
            continue;
        }

        break;
    }

    if (tempinfo == nullptr){
        perror("Failed to bind server to any socket");
    }
    freeaddrinfo(srvinfo);
    pollItems[NETWORK_SRV].fd = srvSock;
    pollItems[NETWORK_SRV].events = POLLIN;

    /* Variable init */
    std::vector<worker_t> busyWorkers;
    std::vector<int> availableWorkers;
    int running = true;
    sockaddr_storage fromAddr;
    socklen_t fromLen;
    size_t bufSize = 0;
    ssize_t bytesRead;
    void *buf;

    /* Allocate buffer memory */
    buf = malloc(MAX_MSG_SIZE);
    if (buf == nullptr) {
        perror("Failed to allocate message buffer");
    }
    else {
        bufSize = MAX_MSG_SIZE;
    }

    /* Signal init complete */
    pthread_barrier_wait(mainData->barrier);

    /* Main proxy loop */
    while(running){
        logMsg("Manager listening for messages.");
        rv = poll(pollItems, POLL_IDS_SIZE, -1);
        if (rv == -1){
            std::string logStr = "ERROR: poll in manager returned non-0. ";
            logStr += std::to_string(errno);
            logMsg(logStr);
            break; // Something weird happened
        }

        // Activity on srvSock
        if (pollItems[NETWORK_SRV].revents & POLLIN){
            bytesRead = recvfrom(srvSock, buf, bufSize, 0, (sockaddr*) &fromAddr, &fromLen);
            if (bytesRead < sizeof(msg_header_t)){
                /* All messages should contain a header at minimum */
                continue;
            }

            msg_header_t *msgHeader;
            msgHeader = static_cast<msg_header_t*>(buf);
            switch (msgHeader->msgType) {
                case MSG_TYPE_PREPARE:
                case MSG_TYPE_COMMIT: {
                    worker_prepare_t *clientMsg;
                    clientMsg = static_cast<worker_prepare_t*>(buf);

                    worker_t *tempWorker;
                    rv = findWorkerWithKey(&tempWorker, busyWorkers, clientMsg->digest);

                    if (rv != 0) {
                        /* Didn't find worker with given key */
                        /* FIXME: How to handle this? */
                        logMsg("Failed to find worker with desired key");
                        break;
                    }

                    /* Forward message to appropriate worker */
                    send(tempWorker->sock, buf, (size_t) bytesRead, 0);
                    break;
                }

                case MSG_TYPE_THREAD_SHUTDOWN:
                case MSG_TYPE_PUT_DATA_REQ:
                case MSG_TYPE_GET_DATA_REQ:
                case MSG_TYPE_PUT_DATA_REP:
                case MSG_TYPE_GET_DATA_REP:
                case MSG_TYPE_PRE_PREPARE:
                case MSG_TYPE_WORKER_FINISHED:
                case MSG_TYPE_GET_DATA_FWD: {
                    if (availableWorkers.size() == 0) {
                        /* FIXME: How to handle this? */
                        logMsg("All workers busy. Dropping request.");
                        break;
                    }

                    /* Configure tempWorker */
                    worker_t tempWorker;
                    tempWorker.sock = availableWorkers.back();
                    if (msgHeader->msgType == MSG_TYPE_PRE_PREPARE) {
                        /* Start of PBFT exchange. Store digest (key) */
                        worker_pre_prepare_t *clientMsg;
                        clientMsg = static_cast<worker_pre_prepare_t *>(buf);
                        tempWorker.currentKey = clientMsg->digest;
                    }

                    /* Send start of new job message to appropriate worker */
                    worker_new_job_msg_t newJob;
                    newJob.reqAddr = fromAddr;
                    newJob.addrLen = fromLen;
                    send(tempWorker.sock, &newJob, sizeof(newJob), 0);

                    /* Send actual message */
                    send(tempWorker.sock, &buf, (size_t) bytesRead, 0);

                    /* Update available/busy workers vectors */
                    availableWorkers.pop_back();
                    busyWorkers.push_back(tempWorker);
                    break;
                }

                default:
                    /* Unrecognized msg type */
                    break;
            }
        }

        /* Activity on workerSock */
        for(i = 0; i < INIT_WORKER_THREAD_CNT; i++) {
            if (pollItems[i].revents & POLLIN) {
                /* Read message from worker */
                bytesRead = recv(pollItems[i].fd, buf, bufSize, 0);
                if(bytesRead < 5){
                    /* FIXME: Handle unexpected message length */
                    continue;
                }

                /* If this is a "READY" message, we are done. Otherwise reply to client */
                if (strncmp(static_cast<char *>(buf), "READY", 5) == 0) {
                    assert(availableWorkers.size() < INIT_WORKER_THREAD_CNT);
                    availableWorkers.push_back(pollItems[i].fd);
                }
            }
        }

        /* Activity on client command socket */
        if (pollItems[CLIENT_CMD].revents  & POLLIN){
            bytesRead = recv(pollItems[CLIENT_CMD].fd, buf, bufSize, 0);
            if (bytesRead < sizeof(msg_header_t)){
                perror("Unexpected message size from client cmd socket.");
                /* FIXME: Flush buffer? */
                continue;
            }

            logMsg("Manager got message from client cmd socket.");
            /* TODO: Write this */
        }
    }

    /* cleanup */
    for(i = 0; i < POLL_IDS_SIZE; i++){
        close(pollItems[i].fd);
    }
    free(buf);
}

void* Node::workerMain(void* arg)
{
    /* FIXME: Convert to standard UDP sockets */
    /* Copy arg data then free arg pointer */
    auto args = static_cast<worker_arg_t*>(arg);
    Node* context = static_cast<Node*>(args->node);
    int id = args->id;
    int managerSock = args->managerSock;

    /* Allocate memory for buffer */
    void *buf;
    size_t bufSize = 0;
    ssize_t bytesRead;

    buf = malloc(MAX_MSG_SIZE);
    if (buf == nullptr){
        perror("Worker failed to allocate memory for buffer");
    }
    else{
        bufSize = MAX_MSG_SIZE;
    }

    /* Notify manager we are ready */
    std::string tempStr;
    tempStr = "READY";
    bytesRead = (tempStr.length() < bufSize)? tempStr.length() : bufSize;
    strncpy((char*)buf, tempStr.c_str(), bytesRead);
    send(managerSock, buf, (size_t) bytesRead, 0);

    std::string logstr = "Worker started with id ";
    logstr.append(std::to_string(id));
    logMsg(logstr);

    /* Signal init complete */
    pthread_barrier_wait(args->barrier);

    int running = true, sendDone = true, result, success;
    while(running) {
        logMsg("Worker listening for message");

        /* Recv new job msg */
        bytesRead = recv(managerSock, buf, bufSize, 0);
        if (bytesRead != sizeof(worker_new_job_msg_t)){
            perror("Worker received unexpected message at job start.");
            /* FIXME: flush queue? */
            continue;
        }

//        /* Recv empty frame, followed by client id */
//        brokerSock->recv(&msg);
//        if(msg.size() != 0){
//            logMsg("Worker received invalid message format. Dropping message.");
//            continue;
//        }
//        zmq_id_t clientID = z_recv_id(brokerSock, ZMQ_NULL);
//
//        /* Data frame */
//        brokerSock->recv(&msg);
//        memcpy(&msgHeader, msg.data(), sizeof(msgHeader));
//
//        logstr = "Worker ";
//        logstr.append(std::to_string(id));
//        logstr.append(" received message. Type: ");
//        logstr.append(std::to_string(msgHeader.msgType));
//        logMsg(logstr);
//
//        /* NOTE: some of the case statements are given their own scope {}
//         *       this is to allow different message type variables to be declared
//         *       depending on the type of message received */
//        switch (msgHeader.msgType) {
//
//            case MSG_TYPE_THREAD_SHUTDOWN:
//                logMsg("Worker shutting down");
//                running = false;
//                /* FIXME: Do any cleanup here */
//                delete brokerSock;
//                break;
//
//            case MSG_TYPE_PRE_PREPARE: {
//                logMsg("Handling pre-prepare message");
//                success = false;
//                size_t dataSize = msg.size() - sizeof(worker_pre_prepare_t);
//                worker_pre_prepare_t *ppMsg = (worker_pre_prepare_t *) malloc(msg.size());
//                if (ppMsg == nullptr) {
//                    /* FIXME: Handle error condition */
//                    break;
//                }
//                memcpy(ppMsg, msg.data(), msg.size());
//
//                /* Send out the prepare messages */
//                worker_prepare_t *pMsg = (worker_prepare_t *) malloc(msg.size() - 3*MSG_TOPIC_SIZE);
//                size_t prepareSize = sizeof(worker_prepare_t) + dataSize;
//                memcpy(pMsg->data,ppMsg->data,dataSize);
//                pMsg->digest = ppMsg->digest;
//                pMsg->msgType = MSG_TYPE_PREPARE;
//                for (int i = 0; i < reqSockCnt - 1; i++){
//                    logMsg("Sending prepare message");
//                    tempAddr = NETWORK_PROTOCOL;
//                    tempAddr += peerArrayToIP(ppMsg->peers[i], IP_ADDR_SIZE);
//                    tempAddr += ':';
//                    tempAddr += PORT;
//                    reqSock[i].sock->connect(tempAddr.c_str());
//                    reqSock[i].curEndpoint = tempAddr;
//
//                    /* FIXME: TOPIC no longer needed */
//                    memcpy(pMsg->msgTopic,ppMsg->peers[i],MSG_TOPIC_SIZE);
//                    zmq::message_t tempMsg(prepareSize);
//                    memcpy(tempMsg.data(), pMsg, prepareSize);
//                    reqSock[i].sock->send(tempMsg);
//                }
//
//                /*Collect incoming prepare messages*/
//                table_entry_t prepMessages[DHT_REPLICATION];
//                prepMessages[0].digest = pMsg->digest;
//                prepMessages[0].data_size = dataSize;
//                prepMessages[0].data_ptr = pMsg->data;
//
//                int num_responses = 1;
//                while (num_responses < DHT_REPLICATION) {
//                    // Poll on all req sockets
//                    result = zmq_poll(&brokerSockPoll, 1, DEFAULT_TIMEOUT_MS);
//
//                    if (result == 0){
//                        // Timeout occured
//                        /* TODO: Gracefully handle timeout */
//                        logMsg("Timeout occurred waiting for prepare messages");
//                    }
//
//                    /* FIXME: Make sure each peer only replies once */
//                    if (brokerSockPoll.revents > 0){
//                        brokerSock->recv(&msg);
//
//                        memcpy(&msgHeader, msg.data(), sizeof(msgHeader));
//                        if (msgHeader.msgType == MSG_TYPE_PREPARE) {
//                            logMsg("Storing a prepare message");
//                            dataSize = msg.size() - sizeof(worker_prepare_t);
//                            prepMessages[num_responses].digest = ((worker_prepare_t *)msg.data())->digest;
//                            prepMessages[num_responses].data_size = dataSize;
//                            prepMessages[num_responses].data_ptr = malloc(dataSize);
//                            memcpy(prepMessages[num_responses].data_ptr,((worker_prepare_t *)msg.data())->data,dataSize);
//
//                            num_responses++;
//                        } else {
//                            logMsg("Throwing away a non-prepare message");
//                        }
//                    }
//                }
//
//                table_entry_t prepareResult;
//                /*Consensus happens here, produces a message to commit*/
//                /* FIXME: 4 is a magic number */
//                if (checkEntryConsensus(prepMessages,4,&prepareResult) == 0){
//                    size_t commitSize = sizeof(worker_commit_t) + prepareResult.data_size;
//                    worker_commit_t *cMsg = (worker_commit_t *) malloc(commitSize);
//                    memcpy(cMsg->data,prepareResult.data_ptr,prepareResult.data_size);
//                    cMsg->digest = prepareResult.digest;
//                    cMsg->msgType = MSG_TYPE_COMMIT;
//
//                    for (int i = 0; i < reqSockCnt - 1; i++){
//                        logMsg("Sending commit message");
//                        /* FIXME: Topic no longer needed */
//                        memcpy(cMsg->msgTopic,ppMsg->peers[i],MSG_TOPIC_SIZE);
//
//                        zmq::message_t tempMsg(commitSize);
//                        memcpy(tempMsg.data(), cMsg, commitSize);
//                        reqSock[i].sock->send(tempMsg);
//                    }
//
//                    table_entry_t commitMessages[DHT_REPLICATION];
//                    commitMessages[0].digest = cMsg->digest;
//                    commitMessages[0].data_size = dataSize;
//                    commitMessages[0].data_ptr = cMsg->data;
//
//                    int num_responses = 1;
//                    while (num_responses < DHT_REPLICATION) {
//                        // Poll on all req sockets
//                        result = zmq_poll(&brokerSockPoll, 1, DEFAULT_TIMEOUT_MS);
//
//                        if (result == 0){
//                            // Timeout occured
//                            /* TODO: Gracefully handle timeout */
//                            logMsg("Timeout occurred waiting for commit messages");
//                        }
//
//                        /* FIXME: Make sure each peer only replies once */
//                        if(brokerSockPoll.revents > 0) {
//                            brokerSock->recv(&msg);
//                            memcpy(&msgHeader, msg.data(), sizeof(msgHeader));
//                            if (msgHeader.msgType == MSG_TYPE_COMMIT) {
//                                logMsg("Storing a commit message");
//                                dataSize = msg.size() - sizeof(worker_commit_t);
//                                commitMessages[num_responses].digest = ((worker_commit_t *) msg.data())->digest;
//                                commitMessages[num_responses].data_size = dataSize;
//                                commitMessages[num_responses].data_ptr = malloc(dataSize);
//                                memcpy(commitMessages[num_responses].data_ptr,
//                                       ((worker_commit_t *) msg.data())->data,
//                                       dataSize);
//
//                                num_responses++;
//                            } else {
//                                logMsg("Throwing away a non-commit message");
//                            }
//                        }
//
//                        table_entry_t commitResult;
//                        if (checkEntryConsensus(commitMessages,4,&commitResult) == 0) {
//                            logMsg("Storing data...");
//                            context->localPut(commitResult.digest, commitResult.data_ptr, commitResult.data_size);
//                            success = true;
//                            for (i = 0; i < DHT_REPLICATION; i++) {free(commitMessages[i].data_ptr);}
//                        } else {
//                            std::cout << "ERROR: Commit Consensus Failed! Aborting..." << std::endl;
//                            break;
//                        }
//                    }
//                    free(cMsg);
//
//                    /* Done with request sockets. Disconnect them */
//                    for(i = 0; i < reqSockCnt - 1; i++){
//                        reqSock[i].sock->disconnect((reqSock[i].curEndpoint).c_str());
//                        reqSock[i].curEndpoint = "";
//                    }
//
//                } else {
//                    std::cout << "ERROR: Prepare Consensus Failed! Aborting..." << std::endl;
//                    free(pMsg);
//                    free(ppMsg);
//                    for (int i = 0; i < DHT_REPLICATION; i++) {free(prepMessages[i].data_ptr);}
//                    break;
//                }
//
//                //worker_commit_t *cMsg = (worker_commit_t *) malloc
//                std::cout << "Finished storing data." << std::endl;
//                free(pMsg);
//                free(ppMsg);
//                for (int i = 0; i < DHT_REPLICATION; i++) {free(prepMessages[i].data_ptr);}
//                break;
//
//                // Send reply to pre-prepare originating node
//                zmq::message_t replyMsg(sizeof(worker_put_rep_msg_t));
//                worker_put_rep_msg_t reply;
//                if(success){
//                    reply.result = true;
//                }
//                else{
//                    reply.result = false;
//                }
//                memcpy(replyMsg.data(), &reply, sizeof(worker_put_rep_msg_t));
//
//                /* Send back reply */
//                z_send_id(brokerSock, clientID);
//                brokerSock->send(replyMsg);
//
//            }
//
//
//            case MSG_TYPE_GET_DATA_FWD: {
//                logMsg("Handling get_fwd message");
//                if (msg.size() < sizeof(worker_get_fwd_msg_t)) {
//                    /* FIXME: Handle error condition */
//                    break;
//                }
//
//                worker_get_fwd_msg_t *getMsg = static_cast<worker_get_fwd_msg_t*>(msg.data());
//                /* localGet returns pointer to data in hash table and its size */
//                int dataSize;
//                void *data;
//                context->localGet(getMsg->digest, &data, &dataSize);
//
//                size_t repSize = sizeof(worker_get_rep_msg_t) + dataSize;
//                worker_get_rep_msg_t *repMsg = (worker_get_rep_msg_t *) malloc(repSize);
//
//                /* FIXME: Topic no longer used */
//                memcpy(repMsg->msgTopic, getMsg->sender, MSG_TOPIC_SIZE);
//
//                repMsg->msgType = MSG_TYPE_GET_DATA_REP;
//                repMsg->digest = getMsg->digest;
//                memcpy(repMsg->data,data,dataSize);
//
//                zmq::message_t tempMsg(repSize);
//                memcpy(tempMsg.data(), repMsg, repSize);
//
//                /* Send back reply */
//                z_send_id(brokerSock, clientID);
//                brokerSock->send(tempMsg);
//
//                free(repMsg);
//                free(data);
//                break;
//            }
//
//            case MSG_TYPE_PUT_DATA_REQ: {
//                logMsg("Worker starting put request.");
//
//                if (msg.size() < sizeof(worker_put_req_msg_t)) {
//                    /* FIXME: Handle error condition */
//                    break;
//                }
//
//                worker_put_req_msg_t *putMsg = (worker_put_req_msg_t *) malloc(msg.size());
//                if (putMsg == nullptr) {
//                    /* FIXME: Handle error condition */
//                    break;
//                }
//                memcpy(putMsg, msg.data(), msg.size());
//
//                logMsg("Worker put request started.");
//
//                /* FIXME: Topic no longer used, but do need to find who to send to */
//                char targetTopic[MSG_TOPIC_SIZE];
//                char tempTopic[MSG_TOPIC_SIZE];
//                std::string tempAddr;
//                memcpy(targetTopic,context->myTopic,3);
//                targetTopic[3] = (char)((((int)putMsg->digest.bytes[CryptoPP::SHA256::DIGESTSIZE-1])%NUM_NODES)+context->myTopic[3]);
//                memcpy(tempTopic,targetTopic,4);
//                size_t ppSize = msg.size() + 3*MSG_TOPIC_SIZE;
//                worker_pre_prepare_t *prePrepareMsg = (worker_pre_prepare_t *) malloc(ppSize);
//                prePrepareMsg->msgType = MSG_TYPE_PRE_PREPARE;
//                size_t dataSize = msg.size() - sizeof(worker_put_req_msg_t);
//
//                int peerCnt = 0;
//                int targetIp;
//                for (int i = 0; i < DHT_REPLICATION; i++) {
//                    logMsg("Creating a pre-prepare message");
//                    targetIp = (int)targetTopic[3] + i;
//                    if (targetIp > NUM_NODES) {targetIp -= NUM_NODES;}
//                    tempTopic[3] = (char)targetIp;
//                    memcpy(prePrepareMsg->msgTopic,tempTopic,4);
//                    for (int j = 0; j < DHT_REPLICATION; j++) {
//                        if (i == j) continue;
//                        targetIp = (int)targetTopic[3] + j;
//                        if (targetIp > NUM_NODES) {targetIp -= NUM_NODES;}
//                        tempTopic[3] = (char)targetIp;
//                        memcpy(prePrepareMsg->peers[peerCnt],tempTopic,4);
//                        peerCnt++;
//                    }
//                    peerCnt = 0;
//                    prePrepareMsg->digest = putMsg->digest;
//                    mempcpy(prePrepareMsg->data, putMsg->data, dataSize);
//                    zmq::message_t tempMsg(ppSize);
//                    memcpy(tempMsg.data(), prePrepareMsg, ppSize);
//
//                    tempAddr = NETWORK_PROTOCOL;
//                    tempAddr += peerArrayToIP(prePrepareMsg->msgTopic, IP_ADDR_SIZE);
//                    tempAddr += ":";
//                    tempAddr += PORT;
//                    reqSock[i].sock->connect(tempAddr.c_str());
//                    reqSock[i].curEndpoint = tempAddr;
//                    reqSock[i].sock->send(tempMsg);
//                }
//
//                /* FIXME: Wait on reply and do something with it */
//                int numResponses = 0;
//                while(numResponses < DHT_REPLICATION){
//                    result = zmq_poll(reqSockPoll, reqSockCnt, DEFAULT_TIMEOUT_MS);
//
//                    if (result == 0){
//                        // Timeout occured
//                        /* TODO: Gracefully handle timeout */
//                        logMsg("Timeout occurred waiting for put request responses.");
//                        break;
//                    }
//
//                    for(i = 0; i < reqSockCnt; i++){
//                        if(reqSockPoll[i].revents > 0) {
//                            (static_cast<zmq::socket_t*> (reqSockPoll[i].socket))->recv(&msg);
//                            memcpy(&msgHeader, msg.data(), sizeof(msgHeader));
//                            if (msgHeader.msgType == MSG_TYPE_PUT_DATA_REP) {
//                                /* FIXME: Gather responses */
//                                numResponses++;
//                            }
//                            else{
//                                logMsg("Throwing away a non-reply message");
//                            }
//                        }
//                    }
//                }
//
//                /* Disconnect request sockets */
//                for(i = 0; i < reqSockCnt; i++){
//                    reqSock[i].sock->disconnect(reqSock[i].curEndpoint.c_str());
//                    reqSock->curEndpoint = "";
//                }
//
//                /* TODO: Evaluate responses and send data back to client here */
//                zmq::message_t replyMsg(sizeof(worker_put_rep_msg_t));
//                worker_put_rep_msg_t putReply;
//                memcpy(replyMsg.data(), &putReply, sizeof(worker_put_rep_msg_t));
//
//                /* Send back reply */
//                z_send_id(brokerSock, clientID);
//                brokerSock->send(replyMsg);
//
//                //context->localPut(putMsg->digest, putMsg->data, dataSize);
//                /* localPut blocks until message is stored in hash table. */
//                /* Safe to free memory at this point */
//                free(putMsg);
//                free(prePrepareMsg);
//                sendDone = 0;
//                break;
//            }
//
//            /* FIXME: Shouldn't get message always be fixed size? */
//            case MSG_TYPE_GET_DATA_REQ: {
//                logMsg("Worker get request started.");
//
//                if (msg.size() < sizeof(worker_get_req_msg_t)) {
//                    /* FIXME: Handle error condition */
//                    break;
//                }
//
//                worker_get_req_msg_t *getMsg = (worker_get_req_msg_t *) malloc(msg.size());
//                if (getMsg == nullptr) {
//                    /* FIXME: Handle error condition */
//                    break;
//                }
//                memcpy(getMsg, msg.data(), msg.size());
//
//                char targetTopic[MSG_TOPIC_SIZE];
//                char tempTopic[MSG_TOPIC_SIZE];
//                memcpy(targetTopic,context->myTopic,3);
//                targetTopic[3] = (char)((((int)getMsg->digest.bytes[CryptoPP::SHA256::DIGESTSIZE-1])%NUM_NODES)+1);
//                memcpy(tempTopic,targetTopic,4);
//                size_t gfSize = sizeof(worker_get_fwd_msg_t);
//                worker_get_fwd_msg_t *getFwdMsg = (worker_get_fwd_msg_t *) malloc(gfSize);
//                getFwdMsg->msgType = MSG_TYPE_GET_DATA_FWD;
//                getFwdMsg->digest = getMsg->digest;
//
//
//                int targetIp;
//                for (int i = 0; i < DHT_REPLICATION; i++) {
//                    logMsg("Creating a get message.");
//
//                    targetIp = (int)targetTopic[3] + i;
//                    if (targetIp > NUM_NODES) {targetIp -= NUM_NODES;}
//                    tempTopic[3] = (char)targetIp;
//                    memcpy(getFwdMsg->msgTopic,tempTopic,4);
//
//                    zmq::message_t tempMsg(gfSize);
//                    memcpy(tempMsg.data(), getFwdMsg, gfSize);
//
//                    std::string tempAddr;
//                    tempAddr = NETWORK_PROTOCOL;
//                    tempAddr += peerArrayToIP(tempTopic, IP_ADDR_SIZE);
//                    tempAddr += ":";
//                    tempAddr += PORT;
//                    reqSock[i].curEndpoint = tempAddr;
//                    reqSock[i].sock->connect(tempAddr.c_str());
//                    reqSock[i].sock->send(tempMsg);
//                }
//
//                /* FIXME: Wait on reply and do something with it */
//                int numResponses = 0;
//                while(numResponses < DHT_REPLICATION){
//                    result = zmq_poll(reqSockPoll, reqSockCnt, DEFAULT_TIMEOUT_MS);
//
//                    if (result == 0){
//                        // Timeout occured
//                        /* TODO: Gracefully handle timeout */
//                        logMsg("Timeout occurred waiting for put request responses.");
//                        break;
//                    }
//
//                    for(i = 0; i < reqSockCnt; i++){
//                        if(reqSockPoll[i].revents > 0) {
//                            (static_cast<zmq::socket_t*> (reqSockPoll[i].socket))->recv(&msg);
//                            memcpy(&msgHeader, msg.data(), sizeof(msgHeader));
//                            if (msgHeader.msgType == MSG_TYPE_GET_DATA_REP) {
//                                /* FIXME: Gather responses */
//                                numResponses++;
//                            }
//                            else{
//                                logMsg("Throwing away a non-reply message");
//                            }
//                        }
//                    }
//                }
//
//                /* Disconnect request sockets */
//                for(i = 0; i < reqSockCnt; i++){
//                    reqSock[i].sock->disconnect(reqSock[i].curEndpoint.c_str());
//                    reqSock->curEndpoint = "";
//                }
//
//                /* TODO: Evaluate responses and send data back to client here */
//                zmq::message_t replyMsg(sizeof(worker_get_rep_msg_t));
//                worker_get_rep_msg_t getReply;
//                memcpy(replyMsg.data(), &getReply, sizeof(worker_get_rep_msg_t));
//
//                /* Send back reply */
//                z_send_id(brokerSock, clientID);
//                brokerSock->send(replyMsg);
//
//                /* localGet blocks until message is retrieved from hash table. */
//                /* Safe to free memory at this point */
//                free(getMsg);
//                free(getFwdMsg);
//                break;
//            }
//
//            default:
//                /* FIXME: Unrecognized message error */
//                break;
//        } /* End switch MSG_TYPE */

        /* FIXME: Is this totally useless now? *?
        /*if(sendDone) {
            worker_msg_header_t finishedMsg;
            zmq::message_t fMsg(sizeof(finishedMsg));
            finishedMsg.msgType = MSG_TYPE_WORKER_FINISHED;
            memcpy(fMsg.data(), &finishedMsg, sizeof(finishedMsg));
            sock->send(fMsg);
        }
        sendDone = 1;
         */
    }
}

int Node::put(std::string key_str, void* data_ptr, int data_bytes)
{
    digest_t digest;
    int result, msgSize;

    /* Get hash digest of key string */
    result = computeDigest(key_str, &digest);
    if (result != 0) {
        logMsg("ERROR: Node::put failed to generate hash digest from key.");
        return -1;
    }

    msgSize = sizeof(worker_put_req_msg_t) + data_bytes;
    worker_put_req_msg_t *putMsg;
    putMsg = static_cast<worker_put_req_msg_t*>(malloc(msgSize));
    if (putMsg == nullptr){
        logMsg("ERROR: Node::put failed to allocate memory.");
        return -1;
    }

    /* Fill out message */
    /* FIXME: Topic no longer used */
    putMsg->msgType = MSG_TYPE_PUT_DATA_REQ;
    putMsg->digest = digest;
    memcpy(putMsg->msgTopic, DEFAULT_TOPIC, sizeof(putMsg->msgTopic));
    memcpy(putMsg->sender, myTopic, sizeof(putMsg->sender)); //FIXME: MY IP
    memcpy(putMsg->data, data_ptr, data_bytes);

    send(this->clientSock, putMsg, msgSize, 0);

    /* FIXME: Is this waiting for ACK or success/fail confirmation?
     * FIXME: Also needs TIMEOUT */
    ssize_t bytesRead;
    worker_put_rep_msg_t reply;
    bytesRead = recv(this->clientSock, &reply, sizeof(reply), 0);
    /* FIXME: Flush buffer if reply message larger than expected? */

    /* TODO: Confirm result matches request? */
    free(putMsg);

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

    /* FIXME: Do we need to wait and compare results here, or has that been done already? */
    /* Send GET_REQ message to node main thread */
    send(this->clientSock, &getMsg, sizeof(getMsg), 0);

    /* FIXME: Worker thread now collects responses. Don't need this */
    /* Wait for responses from node main thread */
//    worker_get_rep_msg_t *getRep;
//    value_t responses[DHT_REPLICATION];
//    int num_responses = 0;
//    zmq::pollitem_t pollSock[1];
//    pollSock[0].socket = this->clientSock;
//    pollSock[0].events = ZMQ_POLLIN;
//    /* FIXME: need to have timeout */
//    while(num_responses < DHT_REPLICATION) {
//#ifdef NODE_DEBUG
//        std::cout << "Node::get waiting for message." << std::endl;
//#endif
//        /* Poll on socket and check for timeout */
//        result = zmq_poll(pollSock, 1, DEFAULT_TIMEOUT_MS);
//        if(pollSock[0].revents <= 0){
//            std::cout << "Node::get timeout while waiting for message." << std::endl;
//            if (num_responses >= 3){
//                break;
//            }
//            else{
//                for (int i = 0; i < num_responses; i++){
//                    free(responses[i].value_ptr);
//                }
//                return -1;
//            }
//        }
//        this->clientSock->recv(&recvMsg);
//        getRep = static_cast<worker_get_rep_msg_t *>(recvMsg.data());
//
//        if (getRep->digest == digest) {
//            responses[num_responses].value_size = recvMsg.size() - sizeof(worker_get_rep_msg_t);
//            responses[num_responses].value_ptr = malloc(responses[num_responses].value_size);
//            if(responses[num_responses].value_ptr == nullptr){
//                std::cout << "ERROR: Node::get failed to allocate memory for responses." << std::endl;
//                for (int i = 0; i < num_responses; i++){
//                    free(responses[i].value_ptr);
//                }
//                return -1;
//            }
//            memcpy(responses[num_responses].value_ptr, getRep->data, responses[num_responses].value_size);
//        }
//        else{
//            /* FIXME: How to best handle this? */
//            //std::cout << "ERROR: Node::get received response with incorrect hash key." << std::endl;
//            //return -1;
//        }
//        num_responses++;
//    }
//
//    value_t answer;
//    result = checkConsensus(responses, DHT_REPLICATION, &answer);
//    int tempInt;
//    for (int i = 0; i < DHT_REPLICATION; i++){
//        tempInt = *((int*)(responses[i].value_ptr));
//        std::cout << "Response " << i << ": " << tempInt << std::endl;
//        free(responses[i].value_ptr);
//    }
//    if (result != 0){
//        /* TODO: Handle case with no consensus */
//        std::cout << "ERROR: Node::get failed to reach a consensus." << std::endl;
//        free(answer.value_ptr);
//        return -1;
//    }
//    if (answer.value_size <= 0){
//        /* TODO: Handle case */
//        std::cout << "ERROR: Node::get received invalid answer data size." << std::endl;
//        free(answer.value_ptr);
//        return -1;
//    }
//    if (answer.value_ptr == nullptr){
//        /* TODO: Handle case */
//        std::cout << "ERROR: Node::get received null answer pointer." << std::endl;
//        free(answer.value_ptr);
//        return -1;
//    }

    /* Receive response */
    worker_get_rep_msg_t *getRep;
    getRep = static_cast<worker_get_rep_msg_t*>(malloc(MAX_MSG_SIZE));
    if (getRep == nullptr){
        perror("Failed to allocate memory for get reply");
        return -1;
    }
    ssize_t bytesRead;
    bytesRead = recv(this->clientSock, getRep, MAX_MSG_SIZE, 0);
    /* FIXME: I'm sure some more checks are needed here */

    /* Check validity */
    value_t response;
    if (getRep->digest == digest) {
        response.value_size = bytesRead - sizeof(worker_get_rep_msg_t);
        if(response.value_size <= 0){
            perror("Received get reply with invalid data size.");
            return -1;
        }

        response.value_ptr = malloc(response.value_size);
        if(response.value_ptr == nullptr){
            std::cout << "ERROR: Node::get failed to allocate memory for responses." << std::endl;
            free(response.value_ptr);
            return -1;
        }
        memcpy(response.value_ptr, getRep->data, response.value_size);
    }
    else{
        /* FIXME: How to best handle this? */
        std::cout << "ERROR: Node::get received response with incorrect hash key." << std::endl;
        return -1;
    }

    /* Copy data to memory block and return */
    /* FIXME: Why is another memory block allocated? Can't we just return response.value_ptr? */
    *(data_ptr) = malloc(response.value_size);
    if (*(data_ptr) == nullptr) {
        std::cout << "ERROR: Node::get failed to allocate memory for data." << std::endl;
        free(response.value_ptr);
        return -1;
    }
    memcpy(*(data_ptr), response.value_ptr, response.value_size);
    *(data_bytes) = response.value_size;
    free(response.value_ptr);

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
        answer->digest = tempDigest;
    }
    else {
        agreementCnt = 1;
        tempDigest = responses[1].digest;
        tempSize = responses[1].data_size;
        tempPtr = responses[1].data_ptr;
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
            answer->digest = tempDigest;
        } else {
            /* No agreement on data size */
            return -1;
        }
    }

    return 0;
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
        std::cout << "TempWorker: " << (tempWorker->currentKey == key) << std::endl;
        if (tempWorker->currentKey == key){
            *in_worker = tempWorker;
            return 0;
        }
    }

    /* No worker currently using specified key */
    return -1;
}

int logMsg(std::string msg)
{
#ifdef NODE_DEBUG
    std::cout << msg<< std::endl;
#endif

    return 0;
}

std::string peerArrayToIP(char *ip, size_t ip_len)
{
    uint8_t temp, i;
    std::string retStr;

    retStr = "";
    for(i = 0; i < ip_len; i++) {
        temp = ip[i];
        retStr += std::to_string(temp);
        if (i < (ip_len - 1)) {
            retStr += '.';
        }
    }

    return retStr;
}