#include "server.h"

#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <chrono>


constexpr int MAX_EVENTS = 64;
constexpr int BUFFER_SIZE = 4096;

Server::Server(int port) {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) throw std::runtime_error("Failed to create socket");

    set_nonblocking(server_fd);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0)
        throw std::runtime_error("Failed to bind socket");

    if (listen(server_fd, SOMAXCONN) < 0)
        throw std::runtime_error("Failed to listen on socket");

    epoll_fd = epoll_create1(0);
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);

    std::cout << "Server listening on port " << port << "...\n";
}

Server::~Server() {
    for (auto& [fd, _] : clients) close(fd);
    if (server_fd != -1) close(server_fd);
    if (epoll_fd != -1) close(epoll_fd);
}

int Server::set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return (flags == -1) ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

std::string Server::current_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&tt, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "[%Y-%m-%d %H:%M:%S]");
    return oss.str();
}

void Server::log_message(const sockaddr_in& addr, const std::string& prefix, const std::string& message) {
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr.sin_addr), ip, sizeof(ip));
    uint16_t port = ntohs(addr.sin_port);
    std::cout << current_timestamp() << " From " << ip << ":" << port << " — " << prefix << ": " << message << "\n";
}

std::string Server::format_double_2dp(double val) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << val;
    return oss.str();
}




void Server::handle_new_connection() {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(server_fd, (sockaddr*)&client_addr, &len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            perror("accept");
            break;
        }

        set_nonblocking(client_fd);

        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
        ev.data.fd = client_fd;

        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
            perror("epoll_ctl ADD client");
            close(client_fd);
            continue;
        }

        clients[client_fd] = Client{"", "", 0, client_addr, false};
        log_message(client_addr, "Connected", "New client connected");
    }
}

void Server::handle_client_data(int client_fd, uint32_t events) {
    auto it = clients.find(client_fd);
    if (it == clients.end()) return;

    Client& client = it->second;

    if (events & (EPOLLERR | EPOLLHUP)) {
        log_message(client.addr, "Disconnected", "Error or hangup");
        close(client_fd);
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
        clients.erase(it);
        return;
    }

    if (events & EPOLLRDHUP) {
        log_message(client.addr, "Peer closed", "Received EPOLLRDHUP");

        if (!client.in_buf.empty()) {
            std::string expr = client.in_buf;
            client.in_buf.clear();
            try {
                double result = calc.calculate(expr);
                std::string response = format_double_2dp(result) + "\n";
                client.out_buf += response;
                log_message(client.addr, "Calculated (last)", expr + " = " + response);
            } catch (const std::exception& e) {
                std::string err_msg = std::string("Error: ") + e.what() + "\n";
                client.out_buf += err_msg;
                log_message(client.addr, "Exception (last)", err_msg);
            }
        }

        if (client.out_buf.empty()) {
            close(client_fd);
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
            clients.erase(it);
        } else {
            client.closing = true;
            epoll_event ev_mod{};
            ev_mod.events = EPOLLOUT | EPOLLET | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
            ev_mod.data.fd = client_fd;
            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &ev_mod);
        }
        return;
    }

    if (events & EPOLLIN) {
        while (true) {
            char buf[BUFFER_SIZE];
            ssize_t count = recv(client_fd, buf, sizeof(buf), 0);
            if (count > 0) {
                client.in_buf.append(buf, count);
                log_message(client.addr, "Received", std::string(buf, count));

                size_t pos;
                while ((pos = client.in_buf.find(' ')) != std::string::npos) {
                    std::string expr = client.in_buf.substr(0, pos);
                    client.in_buf.erase(0, pos + 1);

                    if (!expr.empty()) {
                        try {
                            double result = calc.calculate(expr);
                            std::string response = format_double_2dp(result) + "\n";
                            client.out_buf += response;
                            log_message(client.addr, "Calculated", expr + " = " + response);
                        } catch (const std::exception& e) {
                            std::string err_msg = std::string("Error: ") + e.what() + "\n";
                            client.out_buf += err_msg;
                            log_message(client.addr, "Exception", err_msg);
                        }
                    }
                }

                if (!client.out_buf.empty()) {
                    epoll_event ev_mod{};
                    ev_mod.events = EPOLLOUT | EPOLLET | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
                    ev_mod.data.fd = client_fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &ev_mod);
                }
            } else if (count == 0 || (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            } else {
                perror("recv");
                close(client_fd);
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                clients.erase(it);
                return;
            }
        }
    }

    if (events & EPOLLOUT) {
        while (client.out_sent < client.out_buf.size()) {
            ssize_t sent = send(client_fd, client.out_buf.data() + client.out_sent,
                                client.out_buf.size() - client.out_sent, 0);
            if (sent > 0) {
                log_message(client.addr, "Sent", client.out_buf.substr(client.out_sent, sent));
                client.out_sent += sent;
            } else if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            } else {
                perror("send");
                close(client_fd);
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                clients.erase(it);
                return;
            }
        }

        if (client.out_sent >= client.out_buf.size()) {
            client.out_buf.clear();
            client.out_sent = 0;

            if (client.closing) {
                log_message(client.addr, "Closing", "Finished sending, closing socket");
                close(client_fd);
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                clients.erase(it);
            } else {
                epoll_event ev_mod{};
                ev_mod.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
                ev_mod.data.fd = client_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &ev_mod);
            }
        }
    }
}

void Server::run() {
    epoll_event events[MAX_EVENTS];
    while (true) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds < 0) {
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            if (fd == server_fd) {
                handle_new_connection();
            } else {
                handle_client_data(fd, events[i].events);
            }
        }
    }
}
