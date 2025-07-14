#pragma once

#include <map>
#include <string>
#include <netinet/in.h>
#include <sys/epoll.h>
#include "ICalc.h"

class Server {
public:
    explicit Server(int port);
    ~Server();

    void run();

private:
    int server_fd = -1;
    int epoll_fd = -1;

    struct Client {
        std::string in_buf;
        std::string out_buf;
        size_t out_sent = 0;
        sockaddr_in addr{};
        bool closing = false;
    };
    std::map<int, Client> clients;

    CalcImpl calc;

    int set_nonblocking(int fd);

    void handle_new_connection();
    void handle_client_data(int client_fd, uint32_t events);

    std::string current_timestamp();
    void log_message(const sockaddr_in& addr, const std::string& prefix, const std::string& message);
    std::string format_double_2dp(double val);
};
