#include <iostream>
#include <cstring>

#include "Chord.h"
#include "Node.h"


typedef struct test_data_t{
    int val1;
    double val2;
    char val3[10];
} test_data_t;

int main() {

    /* Start and run Node in separate thread */
    auto node = new Node();
    node->startup();

    std::string userIn, key, valStr;
    int value;
    size_t data_size;
    worker_get_msg_t *getMsg;
    worker_put_msg_t *putMsg;
    digest_t digest;

    while(1){
        std::cout << "Waiting for command: " << std::endl;
        std::cin >> userIn;
        //std::cout << "Got input: " << userIn << std::endl;

        if(userIn.compare("get") == 0){
            std::cout << "Waiting for key: " << std::endl;
            std::cin >> key;
            node->computeDigest(key, &digest);
            getMsg = (worker_get_msg_t*)malloc(sizeof(worker_get_msg_t));
            getMsg->msgType = MSG_TYPE_GET_DATA_REQ;
            getMsg->digest = digest;
            std::cout << "Sending get message with size: " << sizeof(worker_get_msg_t) << std::endl;
            node->send(DEFAULT_TOPIC, (char*)getMsg, sizeof(worker_get_msg_t));
        }
        else if(userIn.compare("put") == 0) {
            std::cout << "Waiting for key: " << std::endl;
            std::cin >> key;
            node->computeDigest(key, &digest);
            std::cin >> valStr;
            value = std::stoi(valStr);
            putMsg = (worker_put_msg_t*)malloc(sizeof(worker_put_msg_t) + sizeof(value));
            putMsg->msgType = MSG_TYPE_PUT_DATA_REQ;
            putMsg->digest = digest;
            *((int*)putMsg->data) = 10;
            std::cout << "Sending put message with size: " << sizeof(worker_put_msg_t) + sizeof(value) << std::endl;
            node->send(DEFAULT_TOPIC, (char*)putMsg, sizeof(worker_put_msg_t) + sizeof(value));
        }
    }


    /********** Debugging code for testing local hash table functionality **********/
//    auto node = new Node();
//    test_data_t test_data;
//    char* data_ptr;
//    int data_size;
//
//    /* Fill basic test data */
//    test_data.val1 = 10;
//    test_data.val2 = 10.1;
//    test_data.val3[0] = '1';
//    test_data.val3[1] = '0';
//    test_data.val3[2] = '-';
//    test_data.val3[3] = '\0';
//
//    data_ptr = (char*)&test_data;
//    data_size = sizeof(test_data);
//
//    node->put("TEST", data_ptr, data_size);
//
//    test_data_t out_data;
//    void* out_ptr;
//    int out_size;
//    node->get("TEST", &out_ptr, &out_size);
//    memcpy(&out_data, out_ptr, (size_t)out_size);
//
//    std::cout << "Retrieved data of size " << out_size << std::endl;
//    std::cout << "\tdata value 1:  " << out_data.val1 << std::endl;
//    std::cout << "\tdata value 2:  " << out_data.val2 << std::endl;
//    std::cout << "\tdata value 3:  " << out_data.val3 << std::endl;
//
//    delete node;

    /********** Debugging code for testing chord functionality **********/
/*
    auto myChord = new Chord();
//    std::cout << "Size of INT on machine: " << sizeof(char) << std::endl;
////    std::cout << "Size of ID: " << sizeof(myId.bytes) << std::endl;
//    for (int i = 0; i < 20; i++) {
//        std::cout << ("%x",(char)myId.bytes[i]);
//    }
//    std::cout << std::endl;
    myChord->getNodeID();
    myChord->getNodeIP();
//    std::cout << "Size of ID: " << sizeof(myId.bytes) << std::endl;
//    for (int i = 0; i < 20;i++) {
//        std::cout << std::hex << (char)myId.bytes[i];
//    }
//    std::cout << std::endl;
    std::cout << "Ideal size of 256 hash: " << std::dec << CryptoPP::SHA256::DIGESTSIZE << std::endl;
*/

    return 0;
}




