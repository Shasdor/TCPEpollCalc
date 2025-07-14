
#include <iostream>
#include <map>
#include <vector>
#include <random>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <iomanip>
#include <chrono>
#include <thread>

#include "client.h"
#include "Generator.h"
#include "ICalc.h"

constexpr int MAX_EVENTS = 64;
constexpr int BUFFER_SIZE = 4096;

struct Conn {
    int fd;
    int id;
    std::string expr;                 
    std::vector<std::string> chunks;
    size_t chunk_index = 0;
    size_t send_offset = 0;
    std::string recv_buffer;
    bool finished_sending = false;
};

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static std::vector<std::string> split_expression_randomly(const std::string& expr, std::mt19937& rng) {
    std::vector<std::string> parts;
    size_t pos = 0;

    while (pos < expr.size()) {
        size_t remaining = expr.size() - pos;
        size_t max_chunk = std::min<size_t>(4, remaining); 
        std::uniform_int_distribution<size_t> dist(1, max_chunk); 
        size_t len = dist(rng);

        parts.emplace_back(expr.substr(pos, len));
        pos += len;
    }

    return parts;
}

static bool double_equal_2dp(double a, double b) {
    return std::fabs(a - b) < 0.005;
}

static ssize_t send_all(int fd, const std::string& data, size_t& offset) {
    ssize_t sent = send(fd, data.data() + offset, data.size() - offset, MSG_NOSIGNAL);
    if (sent > 0) {
        offset += sent;
    }
    return sent;
}

Client::Client(int n, int connections, const std::string& server_ip, int server_port)
    : n_(n), connections_(connections), server_ip_(server_ip), server_port_(server_port) {}

void Client::run() {
    Generator generator;
    CalcImpl evaluator;

    int epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1");
        throw std::runtime_error("Failed to create epoll instance");
    }

    std::map<int, Conn> conns;

    for (int i = 0; i < connections_; ++i) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            perror("socket");
            continue;
        }

        sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(server_port_);
        if (inet_pton(AF_INET, server_ip_.c_str(), &serv_addr.sin_addr) <= 0) {
            std::cerr << "Invalid IP address\n";
            close(sockfd);
            continue;
        }

        if (set_nonblocking(sockfd) < 0) {
            perror("set_nonblocking");
            close(sockfd);
            continue;
        }

        int res = connect(sockfd, (sockaddr*)&serv_addr, sizeof(serv_addr));
        if (res < 0 && errno != EINPROGRESS) {
            perror("connect");
            close(sockfd);
            continue;
        }

        std::string expr = generator.generate_expression(n_);
        std::string expr_to_send = expr + ' ';
        std::string expr_for_check = expr;

        std::random_device rd;
        std::mt19937 rng(rd());

        Conn c;
        c.fd = sockfd;
        c.id = i;
        c.expr = expr_for_check;
        c.chunks = split_expression_randomly(expr_to_send, rng);
        c.chunk_index = 0;
        c.send_offset = 0;
        c.finished_sending = false;

        conns[sockfd] = std::move(c);

        epoll_event ev{};
        ev.data.fd = sockfd;
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev) < 0) {
            perror("epoll_ctl");
            close(sockfd);
            conns.erase(sockfd);
            continue;
        }

        std::cerr << "[Client #" << i << "] Expression: " << expr_for_check << "\n";
    }

    epoll_event events[MAX_EVENTS];
    while (!conns.empty()) {
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, 5000);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }
        if (nfds == 0) continue;

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            auto it = conns.find(fd);
            if (it == conns.end()) continue;
            Conn& c = it->second;

            if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                std::cerr << "[Client #" << c.id << "] Connection closed or error\n";
                close(fd);
                epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                conns.erase(it);
                continue;
            }

            if ((events[i].events & EPOLLOUT) && !c.finished_sending) {
                if (c.chunk_index < c.chunks.size()) {
                    ssize_t sent = send_all(fd, c.chunks[c.chunk_index], c.send_offset);
                    if (sent > 0) {
                        if (c.send_offset == c.chunks[c.chunk_index].size()) {
                            c.chunk_index++;
                            c.send_offset = 0;
                        }
                    } else if (sent == -1 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
                        perror("send");
                        close(fd);
                        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                        conns.erase(it);
                        continue;
                    }

                    epoll_event ev_mod{};
                    ev_mod.data.fd = fd;
                    ev_mod.events = EPOLLIN | EPOLLET;
                    if (c.chunk_index < c.chunks.size()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        ev_mod.events |= EPOLLOUT;
                    } else {
                        c.finished_sending = true;
                    }
                    if (epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev_mod) < 0) {
                        perror("epoll_ctl MOD");
                        close(fd);
                        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                        conns.erase(it);
                        continue;
                    }
                }
            }

            if (events[i].events & EPOLLIN) {
                bool closed = false;
                while (true) {
                    char buf[BUFFER_SIZE];
                    ssize_t recvd = recv(fd, buf, sizeof(buf), 0);
                    if (recvd > 0) {
                        c.recv_buffer.append(buf, recvd);
                    } else if (recvd == 0) {
                        closed = true;
                        break;
                    } else if (recvd == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        break;
                    } else {
                        perror("recv");
                        closed = true;
                        break;
                    }
                }

                size_t pos;
                while ((pos = c.recv_buffer.find('\n')) != std::string::npos) {
                    std::string response_line = c.recv_buffer.substr(0, pos);
                    c.recv_buffer.erase(0, pos + 1);

                    while (!response_line.empty() && std::isspace(response_line.back()))
                        response_line.pop_back();

                    try {
                        double expected = evaluator.calculate(c.expr);
                        double actual = std::stod(response_line);

                        if (!double_equal_2dp(expected, actual)) {
                            std::cerr << "[Client #" << c.id << "] MISMATCH: expr=" << c.expr
                                      << " expected=" << std::fixed << std::setprecision(2) << expected
                                      << " got=" << response_line << "\n";
                        } else {
                            std::cerr << "[Client #" << c.id << "] OK: expr=" << c.expr
                                      << " result=" << std::fixed << std::setprecision(2) << actual << "\n";
                        }
                    } catch (...) {
                        std::cerr << "[Client #" << c.id << "] ERROR: invalid response or calculation\n";
                    }

                    close(fd);
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                    conns.erase(it);
                    break;
                }

                if (closed) {
                    close(fd);
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                    conns.erase(it);
                }
            }
        }
    }

    close(epfd);
}