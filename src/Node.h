//
// Created by charles on 11/9/17.
//

#ifndef BFDHT_NODE_H
#define BFDHT_NODE_H

//#define NODE_DEBUG
#define MAX_KEY_LEN 256         // 256 character key length
#define MAX_DATA_SIZE 1024     // 1 kB max data size

#include "crypto++/sha.h"

/* SHA generated digest type */
typedef struct digest_t{
    /* "byte" type is actually unsigned char */
    byte bytes[CryptoPP::SHA256::DIGESTSIZE];

    /* digest_t used as key for map. Must define < operator */
    /* Unsigned chars are (apparently) auto converted to signed int for comparison */
    bool operator< (digest_t const& other) const {
        for (int i=0; i<sizeof(bytes); i++){
            if (bytes[i] < other.bytes[i]) return true;
            else if (bytes[i] > other.bytes[i]) return false;
            /* Only continue loop if bytes are equal */
        }
        return false; /* Values are equal */
    }
} digest_t;

/* Data type for elements stored in the Hash Table */
typedef struct value_t{
    void* value_ptr;  /* Pointer to start of value data */
    int value_size; /* Size of data stored at value_ptr in bytes */
} value_t;

class Node
{
public:
    /* Public Functions */
    Node();
    ~Node();
    int put(std::string key_str, void* data_ptr, int data_bytes);
    int get(std::string key_str, void** data_ptr, int* data_bytes);

    /* Public Variables */


private:
    /* Private Functions */
    int computeDigest(std::string key_str, digest_t* digest);

    /* Private Variables */
    CryptoPP::SHA256 hash;
    std::map<digest_t, value_t> table;
};


#endif //BFDHT_NODE_H
