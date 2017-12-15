/*
Contains functions implementing the Chord KBR scheme

Written by Tim Krentz, 11/15/2017
*/


#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <cstring>
#include <Chord.h>
#include <iostream>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <crypto++/sha.h>

Chord::Chord()
{

}

Chord::~Chord()
{

}

int Chord::getNodeID()
{
    /*MAC address retrieved with code borrowed from
    https://stackoverflow.com/questions/1779715/how-to-get-mac-address-of-your-machine-using-a-c-program
    */
    struct ifreq ifr;
    struct ifconf ifc;
    char buf[1024];
    int success = 0;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if(sock == -1) perror("ERROR (sock == -1) getNodeID");

    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;
    if (ioctl(sock, SIOCGIFCONF, &ifc) == -1) perror("ERROR (ioctl(sock, SIOCGIFCONF, &ifc) == -1) getNodeID");

    struct ifreq* it = ifc.ifc_req;
    const struct ifreq* const end = it + (ifc.ifc_len / sizeof(struct ifreq));

    for (; it != end; ++it) {
        strcpy(ifr.ifr_name, it->ifr_name);
        if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0) {
            if (! (ifr.ifr_flags & IFF_LOOPBACK)) { // don't count loopback
                if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
                    success = 1;
                    break;
                }
            }
        }
        else perror("ERROR (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0) getNodeID");
    }

    unsigned char mac_address[6];

    if (success) {
        memcpy(mac_address, ifr.ifr_hwaddr.sa_data, 6);
        std::cout << "Using MAC address ";
        for(int i = 0; i < 6; i++){
            std::cout << std::hex << (int)mac_address[i];
        }
        std::cout << "\n";

        /*Hash MAC to get Chord ID*/
        this->hasher.CalculateDigest(this->myId.key.bytes, (byte*)mac_address, 6);


    } else {
        std::cerr << "Couldn't get MAC" << std::endl;
        return -1;
    }

}

int Chord::getNodeIP()
{
    struct ifaddrs * ifAddrStruct=NULL;
    struct ifaddrs * ifa=NULL;
    void * tmpAddrPtr=NULL;

    int success = 0;
    char target_interface[]  = NETWORK_INTERFACE;

    getifaddrs(&ifAddrStruct);

    for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) {
            continue;
        }
        if (ifa->ifa_addr->sa_family == AF_INET) { // check it is IP4
            // is a valid IP4 Address
            tmpAddrPtr=&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            char addressBuffer[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
//            printf("%s IP Address %s\n", ifa->ifa_name, addressBuffer);
            if (strstr(ifa->ifa_name,target_interface) != nullptr) {
//                memcpy(this->myIp.ip,addressBuffer,INET_ADDRSTRLEN);
//                printf("%s IP Address %s\n", ifa->ifa_name, addressBuffer);
                this->myId.ip = addressBuffer;
                success = 1;
                break;
            }
        }
    }
    if (ifAddrStruct!=NULL) freeifaddrs(ifAddrStruct);

        std::cout << "Using IP address " << this->myId.ip << std::endl;

    return success;
}

std::string Chord::getIP()
{
    return myId.ip;
}

void Chord::join(chord_t* bootstrap = nullptr)
{
    /* if i am the bootstrap, do nothing
     * if I am not, alert my bootstrap that I am there and set bootstrap as my s and p
     *
    */
//    if (bootstrap) {
//        initFingerTable(bootstrap);
//        updateOthers();
//    } else {
//        for(int i = 0; i < FINGER_TABLE_SIZE; i++ ){
//            this->finger[i] = this->myId;
//        }
//        this->predecessor = this->myId;
//    }

    //Get last number of IPv4 address
    char* token;
    char strarray[100];
    for(int i = 0; i < this->myId.ip.length(); i++) {
        strarray[i] = this->myId.ip[i];
    }

    token = std::strtok(strarray,".");
    for (int i = 0;i < 3; i++) {
        token = std::strtok(NULL,".");
    }

    int lastIp = atoi(token);
    //std::cout << lastIp << std::endl;
    //this->myId.key.bytes[CryptoPP::SHA256::DIGESTSIZE - 1] = (char)()
    for (int i = 0;i < FINGER_TABLE_SIZE; i++) {
        //this->finger[i]
    }

}

void Chord::updateOthers()
{

}

void Chord::initFingerTable(chord_t* bootstrap)
{

}

void Chord::findSuccessor(digest_t id)
{
    chord_t pred = findPredecessor(id);
    //return pred.successor;
}

void Chord::returnSucessor() {

}

/*Searches the network to find the predecessor of a Chord key
 *
 * BLOCKING*/
chord_t Chord::findPredecessor(digest_t id)
{
    digest_t tempId = this->myId.key;
    digest_t tempSuccesssorId = this->finger[0].key;
    while (!isInRange(tempId,tempSuccesssorId,id)) {
        //FIXME: Institute chord message sends/blocks
        //tempId = tempId.closestPrecedingFinger(id);
        //tempSuccesssorId = tempId.successor();
    }
}

/*Searches local finger table for the finger closest to an ID
 * that is before said ID. Worst case returns its own ID*/
chord_t Chord::closestPrecedingFinger(digest_t id)
{
    for (int i = FINGER_TABLE_SIZE-1; i > 0 ; i--) {
        if (isInRange(this->myId.key,id,this->finger[i].key)) {
            return this->finger[i];
        }
    }
    /*Not within finger table, return self (you are the predecessor)*/
    return this->myId;
}

/* Checks whether a Chord key is within a range, works across
 * the circular discontinuity*/
bool Chord::isInRange(digest_t begin, digest_t end,digest_t id)
{
    return ((id > begin && (id < end || end < begin)) || id < end && (id > begin || end < begin));
}


