#include <iostream>
#include "Node.h"

int main() {
    Node node;
    int get_val = 0;

    node.put("TEST", 10);

    get_val = node.get("TEST");

    std::cout << get_val << std::endl;

    return 0;
}