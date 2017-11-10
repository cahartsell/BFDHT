//
// Created by charles on 11/9/17.
//

#include <iostream>
#include "Node.h"
#include "crypto++/sha.h"

Node::Node()
{
}

Node::~Node()
{

}

void print_digest(digest_t digest){
    int i;

    std::cout << std::endl;
    for (i=0; i<CryptoPP::SHA256::DIGESTSIZE; i++){
        std::cout << ((char) digest.bytes[i]) << std::endl;
    }
    std::cout << std::endl;
}

int Node::put(std::string key_str, int value) {
    /* Convert key std::string to C-string */
    int key_size = key_str.length() + 1;
    char *key = new char[key_size];
    std::strcpy(key, key_str.c_str());

    /* Use hashing function on value to calculate digest */
    hash.CalculateDigest(digest.bytes, (byte*) key, key_size);

    print_digest(digest);

    data[digest] = value;

    return 0;
}

int Node::get(std::string key_str)
{
    /* Convert key std::string to C-string */
    int key_size = key_str.length() + 1;
    char *key = new char[key_size];
    std::strcpy(key, key_str.c_str());

    /* Use hashing function on value to calculate digest */
    hash.CalculateDigest(digest.bytes, (byte*) key, key_size);

    print_digest(digest);

    /* Get stored value */
    int value;
    value = data[digest];

    return value;
}

