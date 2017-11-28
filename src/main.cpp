#include <iostream>
#include <cstring>
#include <Chord.h>
#include "../include/Node.h"

typedef struct test_data_t{
    int val1;
    double val2;
    char val3[10];
} test_data_t;

int main() {
    Node node;
    test_data_t test_data;
    char* data_ptr;
    int data_size;

    /* Fill basic test data */
    test_data.val1 = 10;
    test_data.val2 = 10.1;
    test_data.val3[0] = '1';
    test_data.val3[1] = '0';
    test_data.val3[2] = '-';
    test_data.val3[3] = '\0';

    data_ptr = (char*)&test_data;
    data_size = sizeof(test_data);

    node.put("TEST", data_ptr, data_size);

    test_data_t out_data;
    void* out_ptr;
    int out_size;
    node.get("TEST", &out_ptr, &out_size);
    memcpy(&out_data, out_ptr, (size_t)out_size);

    std::cout << "Retrieved data of size " << out_size << std::endl;
    std::cout << "\tdata value 1:  " << out_data.val1 << std::endl;
    std::cout << "\tdata value 2:  " << out_data.val2 << std::endl;
    std::cout << "\tdata value 3:  " << out_data.val3 << std::endl;

    delete &node;

    Chord myChord;
    //auto myChord = new Chord::Chord();

    return 0;
}