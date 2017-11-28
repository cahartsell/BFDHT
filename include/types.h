//
// Created by timkrentz on 11/27/17.
//

#ifndef BFDHT_TYPES_H
#define BFDHT_TYPES_H

#include <crypto++/sha.h>

/* SHA generated digest type */
typedef struct digest_t{
    byte bytes[CryptoPP::SHA256::DIGESTSIZE]; /* "byte" type is actually unsigned char */

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

#endif //BFDHT_TYPES_H
