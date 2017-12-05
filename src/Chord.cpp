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
        this->hasher.CalculateDigest(this->myId.bytes, (byte*)mac_address, 6);


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
            if (strcmp(target_interface,ifa->ifa_name) == 0) {
//                memcpy(this->myIp.ip,addressBuffer,INET_ADDRSTRLEN);
//                printf("%s IP Address %s\n", ifa->ifa_name, addressBuffer);
                this->myIp.ip = addressBuffer;
                success = 1;
                break;
            }
        }
    }
    if (ifAddrStruct!=NULL) freeifaddrs(ifAddrStruct);

        std::cout << "Using IP address " << this->myIp.ip << std::endl;

    return success;
}

void Chord::join(chord_t* bootstrap = nullptr)
{
    /* if i am the bootstrap, do nothing
     * if I am not, alert my bootstrap that I am there and set bootstrap as my s and p
     *
    */
    if (bootstrap) {

    } else {
        for(int i = 0; i < FINGER_TABLE_SIZE; i++ ){
            this->finger[i] = this->myIp;
        }
        this->predecessor = this->myIp;
    }
}

