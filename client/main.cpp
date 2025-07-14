#include "client.h"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " <n> <connections> <server_addr> <server_port>\n";
        return 1;
    }

    int n = std::atoi(argv[1]);
    int connections = std::atoi(argv[2]);
    std::string server_ip = argv[3];
    int server_port = std::atoi(argv[4]);

    if (n <= 0 || connections <= 0 || server_port <= 0 || server_port > 65535) {
        std::cerr << "Invalid input parameters\n";
        return 1;
    }

    try {
        Client client(n, connections, server_ip, server_port);
        client.run();
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
