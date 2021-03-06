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

    bool operator== (const digest_t& other) const {
        for (int i=0; i<CryptoPP::SHA256::DIGESTSIZE; i++){
            if (this->bytes[i] != other.bytes[i]) return false;
        }
        return true; /* Values are equal */
    }

} digest_t;

typedef struct table_entry_t {
    table_entry_t() : data_ptr(nullptr), data_size(0) {}
    void* data_ptr;
    size_t data_size;
    digest_t digest;

    /* Define comparison operator for table_entry_t */
    bool operator== (const table_entry_t& other) const {
        if (!(this->digest == other.digest)){
            return false; /* Digests don't match */
        }
        if (this->data_size != other.data_size) {
            return false; /* Sizes don't match */
        }
        if (this->data_ptr == nullptr || other.data_ptr == nullptr) {
            return false; /* Invalid data ptr */
        }
        if (memcmp(this->data_ptr, other.data_ptr, this->data_size) != 0){
            return false; /* Data is different */
        }
        return true; /* Structs match */
    }
} table_entry_t;

/* Data type for elements stored in the Hash Table */
typedef struct value_t{
    value_t() : value_ptr(nullptr), value_size(0) {}    /* Constructor & default values */
    void* value_ptr;                                    /* Pointer to start of value data */
    int value_size;                                     /* Size of data stored at value_ptr in bytes */

    /* Define comparison operator for table_entry_t */
    bool operator== (const value_t& other) const {
        if (this->value_size != other.value_size) {
            return false; /* Sizes don't match */
        }
        if (this->value_ptr == nullptr || other.value_ptr == nullptr) {
            return false; /* Invalid data ptr */
        }
        if (memcmp(this->value_ptr, other.value_ptr, this->value_size) != 0){
            return false; /* Data is different */
        }
        return true; /* Structs match */
    }
} value_t;

typedef struct chord_t{
    std::string ip;
    digest_t key;
} chord_t;

#endif //BFDHT_TYPES_H
