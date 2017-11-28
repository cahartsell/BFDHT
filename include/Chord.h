//
// Created by timkrentz on 11/27/17.
//

#ifndef BFDHT_CHORD_H
#define BFDHT_CHORD_H

#include <crypto++/sha.h>
#include <types.h>


class Chord
{
public:
    Chord();
    ~Chord();
    int getNodeID(digest_t* chordId);
private:
    CryptoPP::SHA256 hasher;
};

#endif //BFDHT_CHORD_H
