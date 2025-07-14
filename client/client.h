#pragma once

#include <string>

class Client {
public:
    Client(int n, int connections, const std::string& server_ip, int server_port);
    void run();
private:
    int n_;
    int connections_;
    std::string server_ip_;
    int server_port_;
};
