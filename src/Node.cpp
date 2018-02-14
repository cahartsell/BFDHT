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
std::string ipCharArrayToStr(char *ip, size_t ip_len);
int logMsg(std::string msg);
int findWorkerWithKey(worker_t** in_worker, std::vector<worker_t> &workers, digest_t &key);
int findWorkerWithSock(std::vector<worker_t>::iterator* in_worker, std::vector<worker_t> &workers, int sock);
int sendShutdown(int sock, pthread_t thread, int timeout_ms);

template<typename TYPE>
int checkConsensus(TYPE *entries, int entryCnt, TYPE **answer);

template<typename MSG_TYPE>
int recvPeerReplies(pollfd *pollSock, void* buf, size_t bufSize,
                    table_entry_t *peerValues, int peerCnt, int timeout_ms);


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

    freeTableMem();
    /* TODO: Cleanup worker threads here */
}

int Node::startup() {
    /* FIXME: Need to confirm pthread_create success */

    /* Setup worker & Control sockets */
    /* Socket init sections based heavily off Beej's Networking and UNIX IPC Guides */
    /* Available at www.beej.us */
    int tempPair[2], rv, i;
    manager_thread_data_t managerArg;
    worker_arg_t workerArg[INIT_WORKER_THREAD_CNT];

    /* Setup worker sockets */
    for(i = 0; i < INIT_WORKER_THREAD_CNT; i++) {
        /* Note: SOCK_DGRAM preserves message boundaries. SOCK_STREAM does not */
        rv = socketpair(AF_UNIX, SOCK_DGRAM, 0, tempPair);
        if (rv < 0){
            perror("Failed to create worker socket pair.");
        }

        managerArg.workerSock[i] = tempPair[0];
        workerArg[i].managerSock = tempPair[1];
    }

    /* Setup client cmd socket */
    /* Note: SOCK_DGRAM preserves message boundaries. SOCK_STREAM does not */
    rv = socketpair(AF_UNIX, SOCK_DGRAM, 0, tempPair);
    if (rv < 0){
        perror("Failed to create CMD socket pair.");
    }

    managerArg.clientSock = tempPair[0];
    this->clientSock = tempPair[1];

    /* Configure timeout on clientSock */
    timeval timeout;
    timeout.tv_sec = DEFAULT_TIMEOUT_MS / 1000;
    timeout.tv_usec = (DEFAULT_TIMEOUT_MS % 1000) * 1000;
    rv = setsockopt(this->clientSock, SOL_SOCKET, SO_RCVTIMEO,
                    (void*) &timeout, sizeof(timeout));
    if (rv < 0){
        perror("Failed to set timeout on client sock.");
    }

    /* Spawn thread for Node::manager() */
    /* NOTE: This is done last to ensure all worker sockets have been created */
    pthread_barrier_t managerBarrier;
    rv = pthread_barrier_init(&managerBarrier, nullptr, 2);
    managerArg.barrier = &managerBarrier;
    pthread_create(&managerThread, nullptr, manager, (void *) &managerArg);
    pthread_barrier_wait(&managerBarrier);

    /* Spawn worker thread pool */
    pthread_barrier_t workerBarrier;
    rv = pthread_barrier_init(&workerBarrier, nullptr, INIT_WORKER_THREAD_CNT + 1);
    for(i = 0; i < INIT_WORKER_THREAD_CNT; i++){
        /* Spawn worker thread */
        workerArg[i].id = i;
        workerArg[i].node = this;
        workerArg[i].barrier = &workerBarrier;
        /* FIXME: Need to confirm pthread_create success */
        pthread_create(&(managerArg.workerThreads[i]), nullptr, workerMain, (void*) &(workerArg[i]));
    }
    pthread_barrier_wait(&workerBarrier);

    pthread_barrier_destroy(&workerBarrier);
    pthread_barrier_destroy(&managerBarrier);
    logMsg("Node startup complete.");
    return 0;
}

int Node::shutdown()
{
    /* TODO: Perform other cleanup? */

    /* FIXME: Manager may need longer timeout to shutdown all workers */
    int rv, retVal = 0;
    rv = sendShutdown(this->clientSock, managerThread, DEFAULT_TIMEOUT_MS);

    if (rv != 0){
        perror("Node failed to shutdown threads cleanly");
        retVal = -1;
    }

    freeTableMem();

    return retVal;
}

void* Node::manager(void *arg)
{
    auto *managerData = static_cast<manager_thread_data_t*>(arg);

    pollfd pollItems[POLL_IDS_SIZE];
    int workerSock[INIT_WORKER_THREAD_CNT], i;
    pthread_t workerThread[INIT_WORKER_THREAD_CNT];

    /* Setup worker sockets */
    for(i = 0; i < INIT_WORKER_THREAD_CNT; i++) {
        workerSock[i] = managerData->workerSock[i];
        workerThread[i] = managerData->workerThreads[i];
        pollItems[i].fd = workerSock[i];
        pollItems[i].events = POLLIN;
    }

    /* Setup poll for client CMD sock */
    int clientSock = managerData->clientSock;
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

    rv = getaddrinfo(nullptr, PORT_STR, &hints, &srvinfo);
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
    std::vector<worker_t> clientWorkers;
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
    pthread_barrier_wait(managerData->barrier);

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
            fromLen = sizeof(fromAddr);
            bytesRead = recvfrom(srvSock, buf, bufSize, 0,
                                 (sockaddr*) &fromAddr, &fromLen);
            if (bytesRead < sizeof(msg_header_t)){
                /* All messages should contain a header at minimum */
                continue;
            }

            msg_header_t *msgHeader;
            msgHeader = static_cast<msg_header_t*>(buf);
            switch (msgHeader->msgType) {
                case MSG_TYPE_PREPARE:
                case MSG_TYPE_COMMIT: {
                    worker_prepare_t *tempMsg;
                    tempMsg = static_cast<worker_prepare_t*>(buf);

                    worker_t *tempWorker;
                    rv = findWorkerWithKey(&tempWorker, busyWorkers, tempMsg->digest);

                    if (rv != 0) {
                        /* Didn't find worker with given key */
                        /* FIXME: How to handle this? */
                        logMsg("Failed to find busy worker with desired key");
                        break;
                    }

                    /* Forward message to appropriate worker */
                    send(tempWorker->sock, buf, (size_t) bytesRead, 0);
                    break;
                }

                case MSG_TYPE_PUT_DATA_REP:
                case MSG_TYPE_GET_DATA_REP: {
                    worker_put_rep_msg_t *tempMsg;
                    tempMsg = static_cast<worker_put_rep_msg_t*>(buf);

                    worker_t *tempWorker;
                    rv = findWorkerWithKey(&tempWorker, clientWorkers, tempMsg->digest);

                    if (rv != 0) {
                        /* Didn't find worker with given key */
                        /* FIXME: How to handle this? */
                        logMsg("Failed to find client worker with desired key");
                        break;
                    }

                    /* Forward message to appropriate worker */
                    send(tempWorker->sock, buf, (size_t) bytesRead, 0);
                    break;
                }

                case MSG_TYPE_THREAD_SHUTDOWN:
                case MSG_TYPE_PUT_DATA_REQ:
                case MSG_TYPE_GET_DATA_REQ:
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
                        /* Start of PBFT exchange. Store digest (key) to busyWorkers */
                        worker_pre_prepare_t *tempMsg;
                        tempMsg = static_cast<worker_pre_prepare_t *>(buf);
                        tempWorker.currentKey = tempMsg->digest;
                    }
                    /* Update available/busy workers vectors */
                    /* Workers performing a client request are stored in their own vector "clientWorkers" */
                    availableWorkers.pop_back();
                    busyWorkers.push_back(tempWorker);

                    /* Send start of new job message to appropriate worker */
                    worker_new_job_msg_t newJob;
                    newJob.reqAddr = fromAddr;
                    newJob.addrLen = fromLen;
                    send(tempWorker.sock, &newJob, sizeof(newJob), 0);

                    /* Send actual message */
                    send(tempWorker.sock, buf, (size_t) bytesRead, 0);
                    break;
                }

                default:
                    /* Unrecognized msg type */
                    break;
            }
        }

        /* Activity on workerSock */
        for(i = WORKER_0; i < INIT_WORKER_THREAD_CNT + WORKER_0; i++) {
            if (pollItems[i].revents & POLLIN) {
                /* Read message from worker */
                bytesRead = recv(pollItems[i].fd, buf, bufSize, 0);
                if(bytesRead < 5){
                    /* FIXME: Handle unexpected message length */
                    continue;
                }

                /* If this is a "READY" message, add worker to available workers.
                 * Also remove from busy workers, if it is present */
                if (strncmp(static_cast<char *>(buf), "READY", 5) == 0) {
                    if (availableWorkers.size() >= INIT_WORKER_THREAD_CNT){
                        perror("avaialbleWorkers vector already full.");
                        for(int j=0; j < availableWorkers.size(); j++){
                            std::cout << availableWorkers[j] << std::endl;
                        }
                        std::cout << "Trying to add: " << pollItems[i].fd << std::endl;
                    }

                    /* Only add worker to available list if it is not already present */
                    /* Case where worker sends multiple READY's without getting a new job
                     * can occur if the worker somehow gets unrecognized junk in its message queue */
                    std::vector<int>::iterator it;
                    it = find(availableWorkers.begin(), availableWorkers.end(), pollItems[i].fd);
                    if (it == availableWorkers.end()){
                        availableWorkers.push_back(pollItems[i].fd);
                    }

                    /* FIXME: This could be done more efficiently with something other than a vector (map maybe) */
                    std::vector<worker_t>::iterator tempWorker;
                    rv = findWorkerWithSock(&tempWorker, busyWorkers, pollItems[i].fd);
                    if (rv == 0) {
                        busyWorkers.erase(tempWorker);
                    } else {
                        rv = findWorkerWithSock(&tempWorker, clientWorkers, pollItems[i].fd);
                        if (rv == 0) {
                            clientWorkers.erase(tempWorker);
                        }
                    }
                    continue;
                }

                msg_header_t *tempHeader;
                tempHeader = static_cast<msg_header_t*>(buf);
                switch (tempHeader->msgType){
                    case MSG_TYPE_PUT_DATA_REP:
                    case MSG_TYPE_GET_DATA_REP:{
                        /* Message to be forwarded back to client */
                        /* TODO: This should work since client is restricted to one active request at a time
                         * Will need to be changed if this restriction is removed */
                        send(pollItems[CLIENT_CMD].fd, buf, bytesRead, 0);
                        break;
                    }
                    default:
                        /* Unexpected message type from worker */
                        break;
                }
            }
        }

        /* Activity on client command socket */
        if (pollItems[CLIENT_CMD].revents & POLLIN){
            bytesRead = recv(pollItems[CLIENT_CMD].fd, buf, bufSize, 0);
            if (bytesRead < sizeof(msg_header_t)){
                perror("Unexpected message size from client cmd socket.");
                /* FIXME: Flush buffer? */
                continue;
            }

            logMsg("Manager got message from client cmd socket.");

            msg_header_t* msgHeader = static_cast<msg_header_t*>(buf);
            switch (msgHeader->msgType){
                case MSG_TYPE_THREAD_SHUTDOWN:
                    /* TODO: More cleanup? */
                    running = false;

                    /* Distribute shutdown message to workers */
                    for(i = WORKER_0; i < INIT_WORKER_THREAD_CNT + WORKER_0; i++) {
                        /* FIXME: If worker is in the middle of a PBFT exchange,
                         * it likely won't process shutdown message before timeout occurs */
                        sendShutdown(pollItems[i].fd, workerThread[i], DEFAULT_TIMEOUT_MS);
                    }
                    break;

                case MSG_TYPE_PUT_DATA_REQ:
                case MSG_TYPE_GET_DATA_REQ: {
                    /* Forward client request to appropriate worker */
                    if (availableWorkers.size() == 0) {
                        /* FIXME: How to handle this? */
                        logMsg("All workers busy. Dropping request.");
                        break;
                    }

                    /* Configure tempWorker */
                    worker_t tempWorker;
                    tempWorker.sock = availableWorkers.back();
                    worker_put_req_msg_t *clientMsg;
                    clientMsg = static_cast<worker_put_req_msg_t*>(buf);
                    tempWorker.currentKey = clientMsg->digest;

                    /* Update available/client workers vectors */
                    availableWorkers.pop_back();
                    clientWorkers.push_back(tempWorker);

                    /* Send start of new job message to appropriate worker */
                    worker_new_job_msg_t newJob;
                    memset(&newJob, 0, sizeof(newJob));
                    ssize_t bytesSent;
                    bytesSent = send(tempWorker.sock, &newJob, sizeof(newJob), 0);
                    if(bytesSent){
                        bytesSent = 1;
                    }

                    /* Send actual message */
                    send(tempWorker.sock, buf, (size_t) bytesRead, 0);
                    break;
                }

                default:
                    /* Unrecognized msg type */
                    break;
            }
        }
    }

    /* cleanup */
    for(i = 0; i < POLL_IDS_SIZE; i++){
        close(pollItems[i].fd);
    }
    free(buf);

    pthread_exit(0);
}

void* Node::workerMain(void* arg)
{
    /* Copy arg data since args not valid after calling pthread_barrier_wait */
    auto args = static_cast<worker_arg_t*>(arg);
    Node* context = static_cast<Node*>(args->node);
    int id = args->id;
    int managerSock = args->managerSock;
    int running = true;

    /* Setup polling struct for managerSock */
    pollfd managerSockPoll;
    managerSockPoll.fd = managerSock;
    managerSockPoll.events = POLLIN;

    /* Allocate memory for buffer and incoming data values */
    void *buf;
    size_t bufSize = 0;
    ssize_t bytesRead, bytesSent;

    buf = malloc(MAX_MSG_SIZE);
    if (buf == nullptr){
        perror("Worker failed to allocate memory for buffer");
        running = false;
    }
    else{
        bufSize = MAX_MSG_SIZE;
    }

    int i;
    table_entry_t prepareValues[DHT_REPLICATION], commitValues[DHT_REPLICATION];
    for(i = 0; i < DHT_REPLICATION; i++){
        prepareValues[i].data_ptr = malloc(MAX_DATA_SIZE);
        commitValues[i].data_ptr = malloc(MAX_DATA_SIZE);
        if (prepareValues[i].data_ptr == nullptr || commitValues[i].data_ptr == nullptr){
            perror("Worker failed to allocate memory for incoming peer message values");
            running = false;
        }
    }

    /* Setup UDP socket for outgoing messages */
    int outSock, rv;
    sockaddr_in outAddr[DHT_REPLICATION];
    socklen_t outAddrLen;

    outSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (outSock < 0){
        perror("Worker failed to create outbound socket.");
        running = false;
    }

    memset(outAddr, 0, sizeof(sockaddr_in) * DHT_REPLICATION);
    for ( i = 0; i < DHT_REPLICATION; i++){
        outAddr[i].sin_family = AF_INET;
        outAddr[i].sin_port = htons(PORT);
    }
    outAddrLen = sizeof(sockaddr_in);

    std::string logstr = "Worker started with id ";
    logstr.append(std::to_string(id));
    logMsg(logstr);

    /* Signal init complete */
    pthread_barrier_wait(args->barrier);

    worker_new_job_msg_t newJobMsg;
    msg_header_t *msgHeader;
    std::string readyStr = "READY";
    while(running) {
        /* Notify manager we are ready */
        bytesSent = (readyStr.length() < bufSize)? readyStr.length() : bufSize;
        strncpy((char*)buf, readyStr.c_str(), (size_t) bytesSent);
        bytesSent = send(managerSock, buf, (size_t) bytesSent, 0);

        logMsg("Worker listening for message");

        /* Recv and store new job msg */
        bytesRead = recv(managerSock, buf, bufSize, 0);
        if (bytesRead != sizeof(worker_new_job_msg_t)){
            perror("Worker received unexpected message at job start.");
            /* FIXME: flush queue? */
            continue;
        }
        newJobMsg = *(static_cast<worker_new_job_msg_t*>(buf));
        ((sockaddr_in*)&(newJobMsg.reqAddr))->sin_port = htons(PORT);

        /* Recv job data */
        bytesRead = recv(managerSock, buf, bufSize, 0);
        if (bytesRead < sizeof(msg_header_t) || bytesRead > MAX_MSG_SIZE){
            perror("Worker received unexpected message size for job data.");
            /* FIXME: flush queue? */
            continue;
        }
        msgHeader = static_cast<msg_header_t*>(buf);

        logstr = "Worker ";
        logstr.append(std::to_string(id));
        logstr.append(" received message. Type: ");
        logstr.append(std::to_string(msgHeader->msgType));
        logMsg(logstr);

        /* NOTE: some of the case statements are given their own scope {}
         *       this is to allow different message type variables to be declared
         *       depending on the type of message received */
        switch (msgHeader->msgType) {

            case MSG_TYPE_THREAD_SHUTDOWN:
                logMsg("Worker shutting down");
                running = false;
                /* FIXME: Do any cleanup here */
                break;

            case MSG_TYPE_PREPARE:
            case MSG_TYPE_COMMIT:
            case MSG_TYPE_PRE_PREPARE: {
                /* FIXME: Cleanup memory management. Minimize use of malloc */
                int prepareRecvCnt = 0, commitRecvCnt = 0, timeout = false;
                int preprepareHandled = false, prepareSuccess = false, commitSuccess = false, continuePoll = true;
                table_entry_t *commitResult = nullptr;
                while(1){
                    rv = poll(&managerSockPoll, 1, DEFAULT_WORKER_TIMEOUT_MS);
                    if (rv == 0){
                        // Timeout occured
                        /* TODO: Gracefully handle timeout */
                        timeout = true;
                    }
                    if (rv < 0){
                        perror("recvPeerReplies() encountered error while polling socket");
                        break;
                    }

                    /* FIXME: Make sure each peer only replies once */
                    if(managerSockPoll.revents & POLLIN) {
                        bytesRead = recv(managerSockPoll.fd, buf, bufSize, MSG_DONTWAIT);
                        if (bytesRead < 0){
                            if(errno == EAGAIN || errno == EWOULDBLOCK){
                                /* No message ready */
                                perror("Worker tried to read empty buffer on manager socket");
                                break;
                            }
                            else{
                                perror("Worker encountered error receiving from manager socket");
                                break;
                            }
                        }
                        else if (bytesRead < sizeof(msg_header_t)){
                            perror("Worker received undersized message");
                            continue;
                        }

                        /* Handle message or store data for later as appropriate */
                        auto *tempHeader = static_cast<msg_header_t*>(buf);
                        size_t dataSize;
                        if (tempHeader->msgType == MSG_TYPE_PRE_PREPARE) {
                            /* Pre-prepare message can be handled immediately */
                            logMsg("Handling a pre-prepare message.");

                            auto ppMsg = static_cast<worker_pre_prepare_t*>(buf);
                            dataSize = bytesRead - sizeof(worker_pre_prepare_t);

                            /* Setup peer addresses */
                            std::string peerIP;
                            for (i = 0; i < DHT_REPLICATION - 1; i++){
                                peerIP = ipCharArrayToStr(ppMsg->peers[i], IP_ADDR_SIZE);
                                rv = inet_aton(peerIP.c_str(), &(outAddr[i].sin_addr));
                                if (rv == 0){
                                    perror("inet_aton() failed interpreting peer IP.");
                                    break;
                                }
                            }

                            /* Construct prepare messages */
                            worker_prepare_t *pMsg;
                            size_t prepareSize = sizeof(worker_prepare_t) + dataSize;
                            pMsg = (worker_prepare_t *) malloc(prepareSize);
                            if (pMsg == nullptr){
                                perror("Worker failed to allocate memory for prepare message.");
                                break;
                            }
                            memcpy(pMsg->data, ppMsg->data, dataSize);
                            pMsg->digest = ppMsg->digest;
                            pMsg->msgType = MSG_TYPE_PREPARE;

                            /* Send prepare messages */
                            for (i = 0; i < DHT_REPLICATION - 1; i++){
                                logMsg("Sending prepare message");
                                bytesSent = sendto(outSock, pMsg, prepareSize, 0,
                                                   (sockaddr*) &(outAddr[i]), outAddrLen);
                                if (bytesSent < 0) {
                                    perror("Worker failed sending out prepare message.");
                                    continue;
                                }
                            }

                            /* FIXME: Need to ensure prepareRecvCnt and commitRecvCnt stay in range */
                            /* Init self prepare value */
                            prepareValues[prepareRecvCnt].digest = pMsg->digest;
                            prepareValues[prepareRecvCnt].data_size = dataSize;
                            memcpy(prepareValues[prepareRecvCnt].data_ptr, pMsg->data, dataSize);
                            prepareRecvCnt++;
                            free(pMsg);
                            preprepareHandled = true;
                        }
                        else if (tempHeader->msgType == MSG_TYPE_PREPARE && !preprepareHandled) {
                            logMsg("Storing a prepare message.");
                            auto tempPtr = static_cast<worker_prepare_t*>(buf);
                            dataSize = bytesRead - sizeof(worker_prepare_t);
                            prepareValues[prepareRecvCnt].digest = tempPtr->digest;
                            prepareValues[prepareRecvCnt].data_size = dataSize;
                            memcpy(prepareValues[prepareRecvCnt].data_ptr, tempPtr->data, dataSize);
                            prepareRecvCnt++;
                        }
                        else if (tempHeader->msgType == MSG_TYPE_COMMIT) {
                            logMsg("Storing a commit message.");
                            auto tempPtr = static_cast<worker_commit_t*>(buf);
                            dataSize = bytesRead - sizeof(worker_commit_t);
                            commitValues[commitRecvCnt].digest = tempPtr->digest;
                            commitValues[commitRecvCnt].data_size = dataSize;
                            memcpy(commitValues[commitRecvCnt].data_ptr, tempPtr->data, dataSize);
                            commitRecvCnt++;
                        }
                        else {
                            logMsg("Throwing away unrelated message");
                        }
                    }

                    /* If we've recv'd and handled the pre-prepare, recv'd enough prepare messages,
                     * but have not yet handled them, then handle prepare messages now. */
                    if (preprepareHandled && !prepareSuccess && prepareRecvCnt >= CONSENSUS_THRESHOLD){
                        if(!timeout && prepareRecvCnt < DHT_REPLICATION) {
                            /* Haven't timed out yet. Keep listening for more prepare messages */
                            continue;
                        }

                        /* We've either received all prepare messages or timed-out, so check for consensus */
                        /* Check if received prepare messages agree */
                        table_entry_t *prepareResult;
                        rv = checkConsensus(prepareValues, prepareRecvCnt, &prepareResult);
                        if (rv != 0) {
                            logMsg("No consensus among prepare messages.");
                            break;
                        }

                        size_t commitSize = sizeof(worker_commit_t) + prepareResult->data_size;
                        if (commitSize > bufSize){
                            perror("Worker commit message size exceeds maximum buffer size.");
                            break;
                        }
                        worker_commit_t *cMsg = static_cast<worker_commit_t*>(buf);
                        memcpy(cMsg->data, prepareResult->data_ptr, prepareResult->data_size);
                        cMsg->digest = prepareResult->digest;
                        cMsg->msgType = MSG_TYPE_COMMIT;

                        for (i = 0; i < DHT_REPLICATION - 1; i++){
                            logMsg("Sending commit message");
                            bytesSent = sendto(outSock, cMsg, commitSize, 0,
                                               (sockaddr*) &(outAddr[i]), outAddrLen);
                        }

                        /* Set self commit value */
                        commitValues[commitRecvCnt].digest = cMsg->digest;
                        commitValues[commitRecvCnt].data_size = prepareResult->data_size;
                        memcpy(commitValues[commitRecvCnt].data_ptr, cMsg->data, prepareResult->data_size);
                        commitRecvCnt++;
                        prepareSuccess = true;
                        timeout = false;
                    }

                    /* If prepare messages have been successfully handled and we have received
                     * enough commit messages, then try to handle them now.
                     * Note: prepareSuccess implies prepreparedHandled, so we don't have to check that.
                     * Also, we break after commit is handled, so don't need to check commitSuccess either. */
                    if (prepareSuccess && commitRecvCnt >= CONSENSUS_THRESHOLD){
                        if(!timeout && prepareRecvCnt < DHT_REPLICATION) {
                            /* Haven't timed out yet. Keep listening for more commit messages */
                            continue;
                        }

                        /* We've either received all commit messages or timed-out, so check for consensus */

                        /* Check if received commit messages agree */
                        rv = checkConsensus(commitValues, commitRecvCnt, &commitResult);

                        if (rv == 0) {
                            logMsg("Storing data...");
                            context->localPut(commitResult->digest, commitResult->data_ptr, commitResult->data_size);
                            commitSuccess = true;
                            timeout = false;
                        } else {
                            logMsg("Failed to reach consensus during commit stage. Aborting put.");
                            break;
                        }
                        break;
                    }

                    if (timeout) {
                        /* Timeout was set and no other handler cleared it so PBFT exchange failed */
                        logMsg("Timeout occurred waiting for peer messages.");
                        break;
                    }
                }

                // Send reply to pre-prepare originating node
                worker_put_rep_msg_t reply;
                reply.msgType = MSG_TYPE_PUT_DATA_REP;
                if(commitSuccess){
                    /* This should be a given, but CLion complains */
                    if (commitResult != nullptr) reply.digest = commitResult->digest;
                    reply.result = true;
                    logMsg("Finished storing data.");
                }
                else{
                    if(!preprepareHandled){
                        logMsg("Pre-prepare message not handled successfully.");
                    } else if (!prepareSuccess){
                        logMsg("Did not reach consensus with prepare messages.");
                        if(prepareRecvCnt < CONSENSUS_THRESHOLD){
                            logMsg("Did not receive enough prepare messages.");
                        }
                    } else if(commitRecvCnt < CONSENSUS_THRESHOLD) {
                        logMsg("Did not receive enough commit messages.");
                    }

                    /* FIXME: PBFT failed, but still need to give best guess at requested digest.
                     * Include this in the NewJobMsg? */
                    reply.result = false;
                    logMsg("Failed storing data.");
                }

                /********** DEBUGGING PRINT STATEMENTS ***************/
//                sockaddr_in *temp = (sockaddr_in*)(&newJobMsg.reqAddr);
//                char* ip = inet_ntoa(temp->sin_addr);
//                std::cout << "IP: " <<  ip << ":" << temp->sin_port << " " << temp->sin_family << std::endl;
//                std::cout << newJobMsg.addrLen << std::endl;

                sendto(outSock, &reply, sizeof(reply), 0,
                       (sockaddr*)&(newJobMsg.reqAddr), newJobMsg.addrLen);

                break;
            }


            /******************************************
             * FIXME: PUT_REQ need to send pre-prepare to self first.
             * If they don't, other node may reply with prepare message before local
             */
            case MSG_TYPE_GET_DATA_FWD: {
                logMsg("Handling get_fwd message");
                if (bytesRead < sizeof(worker_get_fwd_msg_t)) {
                    /* FIXME: Handle error condition */
                    perror("Worker received mis-formed message.");
                    break;
                }

                worker_get_fwd_msg_t *getMsg;
                getMsg = static_cast<worker_get_fwd_msg_t*>(buf);
                /* localGet returns pointer to data in hash table and its size */
                int dataSize;
                void *data;
                rv = context->localGet(getMsg->digest, &data, &dataSize);
                if (rv < 0){
                    perror("Local get failed while processing get fwd message.");
                    break;
                }

                size_t repSize = sizeof(worker_get_rep_msg_t) + dataSize;
                if (repSize > bufSize){
                    perror("Worker reply message size exceeds maximum buffer size.");
                    free(data);
                    break;
                }
                worker_get_rep_msg_t *repMsg;
                repMsg = static_cast<worker_get_rep_msg_t*>(buf);

                repMsg->msgType = MSG_TYPE_GET_DATA_REP;
                memcpy(repMsg->data, data, dataSize);
                free(data);

                /********** DEBUGGING PRINT STATEMENTS ***************/
//                sockaddr_in *temp = (sockaddr_in*)(&newJobMsg.reqAddr);
//                char* ip = inet_ntoa(temp->sin_addr);
//                std::cout << "IP: " <<  ip << ":" << temp->sin_port << " " << temp->sin_family << std::endl;
//                std::cout << newJobMsg.addrLen << std::endl;

                /* Send back reply */
                bytesSent = sendto(outSock, repMsg, repSize, 0,
                                  (sockaddr*)&newJobMsg.reqAddr, newJobMsg.addrLen);
                break;
            }

            case MSG_TYPE_PUT_DATA_REQ: {
                logMsg("Worker starting put request.");
                if (bytesRead < sizeof(worker_put_req_msg_t)) {
                    /* FIXME: Handle error condition */
                    perror("Worker received mis-formed put request message.");
                    break;
                }

                worker_put_req_msg_t *putMsg;
                putMsg = static_cast<worker_put_req_msg_t*>(buf);

                logMsg("Worker put request started.");

                /* FIXME: Topic no longer used, but do need to find who to send to */
                char targetTopic[MSG_TOPIC_SIZE];
                char tempTopic[MSG_TOPIC_SIZE];
                std::string tempAddr;
                memcpy(targetTopic,context->myTopic,3);
                targetTopic[3] = (char)((((int)putMsg->digest.bytes[CryptoPP::SHA256::DIGESTSIZE-1])%NUM_NODES)+context->myTopic[3]);
                memcpy(tempTopic,targetTopic,4);

                size_t ppSize = bytesRead + 3*MSG_TOPIC_SIZE;
                size_t dataSize = bytesRead - sizeof(worker_put_req_msg_t);
                worker_pre_prepare_t *prePrepareMsg;
                prePrepareMsg = (worker_pre_prepare_t*) malloc(ppSize);
                if (prePrepareMsg == nullptr){
                    perror("Worker failed to allocate memory for sending pre-prepare messages.");
                    break;
                }
                prePrepareMsg->msgType = MSG_TYPE_PRE_PREPARE;

                int peerCnt = 0, targetIp;
                std::string ipStr;
                for (i = 0; i < DHT_REPLICATION; i++) {
                    logMsg("Creating a pre-prepare message");
                    targetIp = (int)targetTopic[3] + i;
                    if (targetIp > NUM_NODES) {targetIp -= NUM_NODES;}
                    tempTopic[3] = (char)targetIp;

                    /* Setup outAddr */
                    ipStr = ipCharArrayToStr(tempTopic, IP_ADDR_SIZE);
                    rv = inet_aton(ipStr.c_str(), &(outAddr[i].sin_addr));
                    if (rv == 0){
                        perror("inet_aton failed to convert IP string to address for pre-prepare message.");
                        continue;
                    }

                    /* Fill peer data */
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

                    bytesSent = sendto(outSock, prePrepareMsg, ppSize, 0,
                                       (sockaddr*) &(outAddr[i]), outAddrLen);
                }
                /* FIXME: Safe to free prePrepareMsg here? */
                free(prePrepareMsg);

                /* FIXME: Wait on reply and do something with it */
                int numResponses = 0;
                worker_put_rep_msg_t peerReplies[DHT_REPLICATION];
                sockaddr_storage repAddr[DHT_REPLICATION];
                socklen_t  repAddrLen[DHT_REPLICATION];
                while(numResponses < DHT_REPLICATION){
                    rv = poll(&managerSockPoll, 1, 2 * DEFAULT_WORKER_TIMEOUT_MS);
                    if (rv == 0){
                        // Timeout occured
                        /* TODO: Gracefully handle timeout */
                        logMsg("Timeout occurred waiting for put reply messages");
                        break;
                    }
                    if (rv < 0){
                        perror("Worker encountered error while polling manager socket.");
                        break;
                    }

                    if (managerSockPoll.revents & POLLIN){
                        bytesRead = recv(managerSock, buf, bufSize, MSG_DONTWAIT);
                        if (bytesRead < 0){
                            if(errno == EAGAIN || errno == EWOULDBLOCK){
                                /* No message ready */
                                logMsg("Worker tried to read empty buffer on manager socket.");
                                break;
                            }
                            else{
                                perror("Worker encountered error receiving from manager socket.");
                                break;
                            }
                        }
                        else if (bytesRead < sizeof(worker_put_rep_msg_t)){
                            perror("Worker received undersized message instead of put reply message.");
                            break;
                        }

                        msg_header_t *tempHeader;
                        tempHeader = static_cast<msg_header_t*>(buf);
                        if (tempHeader->msgType == MSG_TYPE_PUT_DATA_REP) {
                            logMsg("Storing a put data reply from peer message.");
                            worker_put_rep_msg_t *tempRep;
                            tempRep = static_cast<worker_put_rep_msg_t*>(buf);

                            peerReplies[numResponses].result = tempRep->result;
                            peerReplies[numResponses].digest = tempRep->digest;
                            numResponses++;
                        }
                        else{
                            logMsg("Throwing away a non-reply message");
                        }
                    }
                }

                worker_put_rep_msg_t *putReply;
                putReply = &peerReplies[0];
                if (numResponses >= CONSENSUS_THRESHOLD){
                    rv = checkConsensus(peerReplies, numResponses, &putReply);
                    if (rv != 0){
                        logMsg("Put replies did not agree.");
                        putReply->result = false;
                    }
                } else {
                    putReply->result = false;
                }

                putReply->msgType = MSG_TYPE_PUT_DATA_REP;
                bytesSent = send(managerSock, putReply, sizeof(worker_put_rep_msg_t), 0);
                break;
            }

            /* FIXME: Shouldn't get message always be fixed size? */
            case MSG_TYPE_GET_DATA_REQ: {
                logMsg("Worker get request started.");

                if (bytesRead < sizeof(worker_get_req_msg_t)) {
                    /* FIXME: Handle error condition */
                    perror("Worker received mis-formed get request message.");
                    break;
                }

                worker_get_req_msg_t *getMsg;
                getMsg = static_cast<worker_get_req_msg_t*>(buf);

                char targetTopic[MSG_TOPIC_SIZE];
                char tempTopic[MSG_TOPIC_SIZE];
                memcpy(targetTopic,context->myTopic,3);
                targetTopic[3] = (char)((((int)getMsg->digest.bytes[CryptoPP::SHA256::DIGESTSIZE-1])%NUM_NODES)+1);
                memcpy(tempTopic,targetTopic,4);

                size_t gfSize = sizeof(worker_get_fwd_msg_t);
                worker_get_fwd_msg_t getFwdMsg;
                getFwdMsg.msgType = MSG_TYPE_GET_DATA_FWD;
                getFwdMsg.digest = getMsg->digest;

                int targetIp;
                std::string ipStr;
                for (i = 0; i < DHT_REPLICATION; i++) {
                    logMsg("Creating a get message.");

                    targetIp = (int)targetTopic[3] + i;
                    if (targetIp > NUM_NODES) {targetIp -= NUM_NODES;}
                    tempTopic[3] = (char)targetIp;

                    /* Setup outAddr */
                    ipStr = ipCharArrayToStr(tempTopic, IP_ADDR_SIZE);
                    rv = inet_aton(ipStr.c_str(), &(outAddr[i].sin_addr));
                    if (rv == 0){
                        perror("inet_aton failed to convert IP string to address for get fwd message.");
                        continue;
                    }

                    bytesSent = sendto(outSock, &getFwdMsg, sizeof(getFwdMsg), 0,
                                       (sockaddr*) (&outAddr[i]), outAddrLen);
                }

                int numResponses = 0;
                rv = recvPeerReplies<worker_get_rep_msg_t>(&managerSockPoll, buf, bufSize, &(commitValues[0]),
                                                           DHT_REPLICATION, DEFAULT_WORKER_TIMEOUT_MS);
                if (rv < 0) {
                    /* FIXME: What else needs to happen here? Send some reply? */
                    perror("Worker failed to recv prepare messages.");
                    break;
                }
                numResponses += rv;

                /* Check for consensus */
                auto *getReply = static_cast<worker_get_rep_msg_t*>(buf);
                table_entry_t *getResult;
                size_t dataSize = 0;
                getReply->msgType = MSG_TYPE_GET_DATA_REP;
                if (numResponses >= CONSENSUS_THRESHOLD) {
                    rv = checkConsensus(commitValues, numResponses, &getResult);
                    if (rv == 0) {
                        getReply->digest = getResult->digest;
                        dataSize = getResult->data_size;
                        memcpy(getReply->data, getResult->data_ptr, dataSize);
                    }
                    else {
                        /* FIXME: Return get failed? */
                        perror("Get replies failed consensus check");
                        break;
                    }
                }

                /* TODO: Evaluate responses and send data back to client here */

                bytesSent = send(managerSock, getReply, sizeof(worker_get_rep_msg_t) + dataSize, 0);
                break;
            }

            default:
                /* FIXME: Handle Unrecognized message error */
                break;
        } /* End switch MSG_TYPE */
    }

    /* TODO: Other cleanup */
    free(buf);
    for (i = 0; i < DHT_REPLICATION; i++){
        free(commitValues->data_ptr);
        free(prepareValues->data_ptr);
    }
    close(managerSock);
    pthread_exit(0);
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
    putMsg->msgType = MSG_TYPE_PUT_DATA_REQ;
    putMsg->digest = digest;
    memcpy(putMsg->data, data_ptr, data_bytes);

    send(this->clientSock, putMsg, msgSize, 0);
    free(putMsg);

    /* FIXME: Is this waiting for ACK or success/fail confirmation? */
    ssize_t bytesRead;
    worker_put_rep_msg_t reply;
    bytesRead = recv(this->clientSock, &reply, sizeof(reply), 0);
    if(bytesRead < 0){
        if (errno == EAGAIN || errno == EWOULDBLOCK){
            /* Timeout occured */
            logMsg("Client request timed out.");
            return -1;
        }
        else{
            perror("Client failed to receive reply.");
            return -1;
        }
    }
    if(bytesRead == 0){
        perror("Client to manager socket is disconnected.");
        return -1;
    }
    if (bytesRead < sizeof(worker_put_rep_msg_t)){
        perror("Client received unexpected message size instead of put reply.");
        return -1;
    }
    /* FIXME: Flush buffer if reply message larger than expected? */

    /* TODO: Confirm result matches request? */

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

    /* FIXME: Do we need to wait and compare results here, or has that been done already? */
    /* Send GET_REQ message to node manager thread */
    send(this->clientSock, &getMsg, sizeof(getMsg), 0);

    /* Receive response */
    /* FIXME: Mem leak occurs if this function does not return successfully. */
    worker_get_rep_msg_t *getRep;
    getRep = static_cast<worker_get_rep_msg_t*>(malloc(MAX_MSG_SIZE));
    if (getRep == nullptr){
        perror("Failed to allocate memory for get reply");
        return -1;
    }
    ssize_t bytesRead;

    bytesRead = recv(this->clientSock, getRep, MAX_MSG_SIZE, 0);
    if(bytesRead < 0){
        if (errno == EAGAIN || errno == EWOULDBLOCK){
            /* Timeout occured */
            logMsg("Client request timed out.");
            return -1;
        }
        else{
            perror("Client failed to receive reply.");
            return -1;
        }
    }
    if(bytesRead == 0){
        perror("Client to manager socket is disconnected.");
        return -1;
    }
    if (bytesRead < sizeof(worker_get_rep_msg_t)){
        perror("Client received unexpected message size instead of get reply.");
        return -1;
    }
    /* FIXME: I'm sure some more checks are needed here */
    /* FIXME: Flush buffer if reply message larger than expected? */

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
    free(getRep);

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
    this->tableMutex.unlock();
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
        return -1;
    }
    this->tableMutex.lock();
    memcpy(*data_ptr, out_value.value_ptr, *data_bytes);
    this->tableMutex.unlock();

    return 0;
}

int Node::computeDigest(std::string key_str, digest_t* digest)
{
    /* Convert key std::string to C-string */
    unsigned long key_size = key_str.length() + 1;
    if (key_str.length() > MAX_KEY_LEN){
        logMsg("computeDigest received key string longer than MAX_KEY_LEN");
        return -1;
    }
    if (key_str.length() <= 0){
        logMsg("computeDigest received key string of length 0 (or negative).");
        return -1;
    }

    /* Use hashing function on value to calculate digest */
    this->hash.CalculateDigest(digest->bytes, (byte*)key_str.c_str(), key_size);

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

template<typename TYPE>
int checkConsensus(TYPE *entries, int entryCnt, TYPE **answer)
{
    if (entryCnt < 3) return -1; /* Should always be at least 3 entries */

    /* FIXME: Generalize for >4 */
    /* This is an embarrassingly bad algorithm and I am ashamed by it */
    int agreementCnt = 1;
    for (int i = 1; i < entryCnt; i++){
        if (entries[0] == entries[i]) agreementCnt++;
    }

    if (agreementCnt >= CONSENSUS_THRESHOLD){
        *answer = &(entries[0]);
    } else {
        agreementCnt = 1;
        if(entries[1] == entries[0]) {
            agreementCnt++;
        }
        for (int i = 2; i < entryCnt; i++) {
            if(entries[1] == entries[i]) {
                agreementCnt++;
            }
        }
        if (agreementCnt >= CONSENSUS_THRESHOLD) {
            *answer = &(entries[1]);
        } else {
            /* No consensus */
            return -1;
        }
    }

    return 0;
}

template<typename MSG_TYPE>
int recvPeerReplies(pollfd *pollSock, void* buf, size_t bufSize,
                    table_entry_t *peerValues, int peerCnt, int timeout_ms)
{
    /* Wait for incoming commit messages */
    int numResponses = 0, rv, timeout = 0;
    ssize_t bytesRead;
    MSG_TYPE tempMsg;
    while (numResponses < peerCnt) {
        rv = poll(pollSock, 1, timeout_ms);
        if (rv == 0){
            // Timeout occured
            /* TODO: Gracefully handle timeout */
            timeout = 1;
            std::string logStr = "Timeout occurred waiting for messages of type: ";
            logStr.append(std::to_string(tempMsg.msgType));
            logMsg(logStr);
            break;
        }
        if (rv < 0){
            perror("recvPeerReplies() encountered error while polling socket");
            return -1;
        }

        /* FIXME: Make sure each peer only replies once */
        if(pollSock->revents & POLLIN) {
            bytesRead = recv(pollSock->fd, buf, bufSize, MSG_DONTWAIT);
            if (bytesRead < 0){
                if(errno == EAGAIN || errno == EWOULDBLOCK){
                    /* No message ready */
                    perror("Worker tried to read empty buffer on manager socket");
                    break;
                }
                else{
                    perror("Worker encountered error receiving from manager socket");
                    break;
                }
            }
            else if (bytesRead < sizeof(MSG_TYPE)){
                perror("Worker received undersized message");
                break;
            }

            auto *tempHeader = static_cast<msg_header_t*>(buf);
            MSG_TYPE *tempPtr;
            size_t dataSize;
            if (tempHeader->msgType == tempMsg.msgType) {
                std::string logStr = "Storing a message of type: ";
                logStr.append(std::to_string(tempMsg.msgType));
                logMsg(logStr);

                tempPtr = static_cast<MSG_TYPE*>(buf);
                dataSize = bytesRead - sizeof(MSG_TYPE);
                peerValues[numResponses].digest = tempPtr->digest;
                peerValues[numResponses].data_size = dataSize;
                memcpy(peerValues[numResponses].data_ptr, tempPtr->data, dataSize);
                numResponses++;
            } else {
                logMsg("Throwing away unrelated message");
            }
        }
    }

    return numResponses;
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
        if (tempWorker->currentKey == key){
            *in_worker = tempWorker;
            return 0;
        }
    }

    /* No worker currently using specified key */
    return -1;
}

/* Similar to findWorkerWithKey, but searches based on socket and sets iterator */
int findWorkerWithSock(std::vector<worker_t>::iterator* in_worker, std::vector<worker_t> &workers, int sock)
{
    /* Input check */
    if (in_worker == nullptr){
        return -1;
    }

    std::vector<worker_t>::iterator it;
    worker_t* tempWorker;
    for(it = workers.begin(); it != workers.end(); it++){
        tempWorker = it.base();
        if (tempWorker->sock == sock){
            *in_worker = it;
            return 0;
        }
    }

    /* No worker currently using specified key */
    return -1;
}

int sendShutdown(int sock, pthread_t thread, int timeout_ms)
{
    /* FIXME: Something in this function makes valgrind panic claiming SIGSEGV */
    /* Distribute shutdown message to workers */
    msg_header_t tempHeader;
    tempHeader.msgType = MSG_TYPE_THREAD_SHUTDOWN;
    ssize_t bytesSent;
    void *retVal;
    int rv;

    timespec timeout;
    timeout.tv_sec = (timeout_ms / 1000);
    timeout.tv_nsec = (timeout_ms % 1000) * 1000;

    bytesSent = send(sock, &tempHeader, sizeof(tempHeader), 0);

    if (bytesSent != sizeof(msg_header_t)) {
        /* Message send failed. Send cancel request. */
        perror("Failed sending shutdown message to thread. Sending cancel");
        pthread_cancel(thread);
    }

    rv = pthread_timedjoin_np(thread, &retVal, &timeout);
    if (rv != 0) {
        if (errno == ETIMEDOUT){
            perror("Thread did not shutdown in time. Sending cancel");
            pthread_cancel(thread);
            return -1;
        } else {
            perror("Thread did not shutdown properly.");
            return -1;
        }
    }

    return 0;
}

int logMsg(std::string msg)
{
#ifdef NODE_DEBUG
    std::cout << msg << std::endl;
#endif

    return 0;
}

std::string ipCharArrayToStr(char *ip, size_t ip_len)
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