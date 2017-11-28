/*
Contains functions implementing the Chord KBR scheme

Written by Tim Krentz, 11/15/2017
*/


#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <Chord.h>
#include <iostream>
#include <types.h>

Chord::Chord()
{

}

Chord::~Chord()
{

}

int Chord::getNodeID(digest_t* chordId)
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
        //CryptoPP::SHA256::Transform(chordId,mac_address);//need to find out how to use HashWordType

        return 0;

    } else {
        std::cerr << "Couldn't get MAC" << std::endl;
        return -1;
    }

}

