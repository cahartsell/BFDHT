//
// Created by timkrentz on 11/27/17.
//

#ifndef BFDHT_CHORD_H
#define BFDHT_CHORD_H

#include <crypto++/sha.h>
#include <types.h>

#define FINGER_TABLE_SIZE 3
#define NETWORK_INTERFACE "eth0"
#define KEYSPACE_SIZE 8

class Chord
{
public:
    Chord();
    ~Chord();
    int getNodeID();
    int getNodeIP();
    std::string getIP();
    void join(chord_t* bootstrap);
private:
    CryptoPP::SHA256 hasher;
    chord_t finger[FINGER_TABLE_SIZE];
    chord_t predecessor;
    chord_t myId;

    void updateOthers();
    void initFingerTable(chord_t* bootstrap);
    void findSuccessor(digest_t id);
    chord_t findPredecessor(digest_t id);
    chord_t closestPrecedingFinger(digest_t id);
    bool isInRange(digest_t begin, digest_t end,digest_t id);
    void returnSucessor();

};

#endif //BFDHT_CHORD_H