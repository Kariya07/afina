#ifndef AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H

#include <cstring>

#include <afina/execute/Command.h>
#include <protocol/Parser.h>
#include <spdlog/logger.h>
#include <sys/epoll.h>

namespace Afina {
namespace Network {
namespace MTnonblock {

class Connection {
public:
    Connection(int s, std::shared_ptr<spdlog::logger> pl, std::shared_ptr<Afina::Storage> ps)
        : _socket(s), _logger(pl), pStorage(ps){
        std::memset(&_event, 0, sizeof(struct epoll_event));
        _event.data.ptr = this;
        is_alive.store(true);
        finish_reading.store(false);
        written = 0;
        readed_bytes = 0;
    }

    inline bool isAlive() const { return is_alive.load(std::memory_order_acquire); }

    void Start();

protected:
    void OnError();
    void OnClose();
    void DoRead();
    void DoWrite();

private:
    friend class Worker;
    friend class ServerImpl;

    int _socket;
    struct epoll_event _event;

    std::atomic<bool> is_alive;
    std::vector<std::string> out;
    size_t written;

    std::shared_ptr<spdlog::logger> _logger;
    std::shared_ptr<Afina::Storage> pStorage;

    // - parser: parse state of the stream
    // - command_to_execute: last command parsed out of stream
    // - arg_remains: how many bytes to read from stream to get command argument
    // - argument_for_command: buffer stores argument
    std::size_t arg_remains;
    Protocol::Parser parser;
    std::string argument_for_command;
    std::unique_ptr<Execute::Command> command_to_execute;
    int readed_bytes;
    char client_buffer[4096];
    std::atomic<bool> finish_reading;
};

} // namespace MTnonblock
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H
