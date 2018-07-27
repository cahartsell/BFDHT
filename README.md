# BFDHT

Byzantine Fault tolerant Distributed Hash Table

## Requirements

Need dev versions of ZMQ (libzmq3-dev) and crypto++ (libcrypto++-dev) libraries to build

Recommend installing dependencies with package manager 

## Cross Compilation

For cross compilation to Beaglebone Black (armhf), follow this [setup guide](http://www.exploringbeaglebone.com/chapter7/).
Setup for other architectures is similar.

For the cross compiled executable to run correctly on the target system, all required libraries must be present on the target system (dev versions not required).
In particular, **libcrypto++9** (Debian Jessie) is older than **libcrypto++6** (Debian Stretch) and the correct version may not be available through the system package manager. 
