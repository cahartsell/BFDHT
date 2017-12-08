//
// Created by timkrentz on 11/27/17.
//

#ifndef BFDHT_CHORD_H
#define BFDHT_CHORD_H

#include <crypto++/sha.h>
#include <types.h>

#define FINGER_TABLE_SIZE 5
#define NETWORK_INTERFACE "eth0"
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
    digest_t myId;
    chord_t finger[FINGER_TABLE_SIZE];
    chord_t predecessor;
    chord_t myIp;
};

#endif //BFDHT_CHORD_H
