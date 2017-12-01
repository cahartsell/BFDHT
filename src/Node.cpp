//
// Created by charles on 11/9/17.
//

#include <iostream>
#include <cstring>
#include <zmq.hpp>

#include "Node.h"

/* Local helper functions */
void print_digest(digest_t digest);

Node::Node()
{
    /* Prepare ZMQ Context */
    zmq_context = new zmq::context_t();

    /*Create Node ID*/

    //update finger table
}

Node::~Node()
{
    freeTableMem();
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
