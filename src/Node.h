//
// Created by charles on 11/9/17.
//

#ifndef BFDHT_NODE_H
#define BFDHT_NODE_H

#include "crypto++/sha.h"

struct digest_t{
    /* "byte" type is actually unsigned char */
    byte bytes[CryptoPP::SHA256::DIGESTSIZE];

    /* Unsigned chars are (apparently) auto converted to signed int for comparison */
    bool operator< (digest_t const& other) const {
        for (int i=0; i<sizeof(bytes); i++){
            if (bytes[i] < other.bytes[i]) return true;
            else if (bytes[i] > other.bytes[i]) return false;
            /* Only continue loop if bytes are equal */
        }

        return false; /* Values are equal */
    }
};

class Node
{
public:
    /* Public Functions */
    Node();
    ~Node();
    int put(std::string key_str, int value);
    int get(std::string key_str);

    /* Public Variables */

private:
    /* Private Functions */

    /* Private Variables */
    CryptoPP::SHA256 hash;
    digest_t digest;

    std::map<digest_t, int> data;
};


#endif //BFDHT_NODE_H
