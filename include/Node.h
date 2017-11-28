//
// Created by charles on 11/9/17.
//

#ifndef BFDHT_NODE_H
#define BFDHT_NODE_H

//#define NODE_DEBUG
#define MAX_KEY_LEN 256         // 256 character key length
#define MAX_DATA_SIZE 1024     // 1 kB max data size

#include <types.h>


/* Data type for elements stored in the Hash Table */
typedef struct value_t{
    value_t() : value_ptr(nullptr), value_size(0) {}    /* Constructor & default values */
    void* value_ptr;                                    /* Pointer to start of value data */
    int value_size;                                     /* Size of data stored at value_ptr in bytes */
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
    void freeTableMem();



    /* Private Variables */
    CryptoPP::SHA256 hash;
    std::map<digest_t, value_t> table;

    /*finger table*/

};


#endif //BFDHT_NODE_H
