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
    /* sorta following https://www.ibm.com/support/knowledgecenter/en/SSLTBW_2.3.0/com.ibm.zos.v2r3.cbclx01/cplr318.htm*/
    bool operator< (const digest_t& other) const {
        for (int i=0; i<CryptoPP::SHA256::DIGESTSIZE; i++){
            if (this->bytes[i] < other.bytes[i]) return true;
            else if (this->bytes[i] > other.bytes[i]) return false;
            /* Only continue loop if bytes are equal */
        }
        return false; /* Values are equal */
    }

    bool operator> (const digest_t& other) const {
        for (int i=0; i<CryptoPP::SHA256::DIGESTSIZE; i++){
            if (this->bytes[i] > other.bytes[i]) return true;
            else if (this->bytes[i] < other.bytes[i]) return false;
            /* Only continue loop if bytes are equal */
        }
        return false; /* Values are equal */
    }

} digest_t;

typedef struct chord_t{
    std::string ip;
    digest_t key;
} chord_t;

#endif //BFDHT_TYPES_H
