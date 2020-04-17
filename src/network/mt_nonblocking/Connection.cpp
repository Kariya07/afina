#include "Connection.h"

#include <iostream>
#include <sys/uio.h>

namespace Afina {
namespace Network {
namespace MTnonblock {

// See Connection.h
void Connection::Start() {
    std::unique_lock<std::mutex> _lock(block_other_connections);
    _logger->debug("Start connection on {} socket", _socket);
    _event.data.fd = _socket;
    _event.data.ptr = this;
    _event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET;
}

// See Connection.h
void Connection::OnError() {
    is_alive.store(false);
    _logger->warn("Error in connection on {} socket", _socket);
}

// See Connection.h
void Connection::OnClose() {
    is_alive.store(false);
    _logger->debug("Stop connection on {} socket", _socket);
}

// See Connection.h
void Connection::DoRead() {
    std::unique_lock<std::mutex> _lock(block_other_connections);
    // - parser: parse state of the stream
    // - command_to_execute: last command parsed out of stream
    // - arg_remains: how many bytes to read from stream to get command argument
    // - argument_for_command: buffer stores argument
    std::size_t arg_remains;
    Protocol::Parser parser;
    std::string argument_for_command;
    std::unique_ptr<Execute::Command> command_to_execute;
    try {
        int readed_bytes = 0;
        int read_bytes = -1;
        char client_buffer[4096];
        while ((read_bytes = read(_socket, client_buffer + readed_bytes, sizeof(client_buffer) - readed_bytes)) > 0) {
            _logger->debug("Got {} bytes from socket", read_bytes);

            // Single block of data readed from the socket could trigger inside actions a multiple times,
            // for example:
            // - read#0: [<command1 start>]
            // - read#1: [<command1 end> <argument> <command2> <argument for command 2> <command3> ... ]
            readed_bytes += read_bytes;
            while (readed_bytes > 0) {
                _logger->debug("Process {} bytes", readed_bytes);
                // There is no command yet
                if (!command_to_execute) {
                    std::size_t parsed = 0;
                    try {
                        if (parser.Parse(client_buffer, readed_bytes, parsed)) {
                            // There is no command to be launched, continue to parse input stream
                            // Here we are, current chunk finished some command, process it
                            _logger->debug("Found new command: {} in {} bytes", parser.Name(), parsed);
                            command_to_execute = parser.Build(arg_remains);
                            if (arg_remains > 0) {
                                arg_remains += 2;
                            }
                        }
                    } catch (std::runtime_error &ex) {
                        out.push_back("(?^u:ERROR)");
                        _event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET | EPOLLOUT;
                        throw std::runtime_error(ex.what());
                    }
                    // Parsed might fails to consume any bytes from input stream. In real life that could happens,
                    // for example, because we are working with UTF-16 chars and only 1 byte left in stream
                    if (parsed == 0) {
                        break;
                    } else {
                        std::memmove(client_buffer, client_buffer + parsed, readed_bytes - parsed);
                        readed_bytes -= parsed;
                    }
                }

                // There is command, but we still wait for argument to arrive...
                if (command_to_execute && arg_remains > 0) {
                    _logger->debug("Fill argument: {} bytes of {}", readed_bytes, arg_remains);
                    // There is some parsed command, and now we are reading argument
                    std::size_t to_read = std::min(arg_remains, std::size_t(readed_bytes));
                    argument_for_command.append(client_buffer, to_read);

                    std::memmove(client_buffer, client_buffer + to_read, readed_bytes - to_read);
                    arg_remains -= to_read;
                    readed_bytes -= to_read;
                }

                // Thre is command & argument - RUN!
                if (command_to_execute && arg_remains == 0) {
                    _logger->debug("Start command execution");

                    std::string result;
                    command_to_execute->Execute(*pStorage, argument_for_command, result);

                    // Send response
                    result += "\r\n";
                    bool is_empty = out.empty();
                    out.push_back(result);
                    if (is_empty) {
                        _event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET | EPOLLOUT;
                    }
                    // Prepare for the next command
                    command_to_execute.reset();
                    argument_for_command.resize(0);
                    parser.Reset();
                }
            } // while (readed_bytes)
        }
        is_alive.store(false);
        if (read_bytes == 0) {
            _logger->debug("Connection closed");
        } else {
            throw std::runtime_error(std::string(strerror(errno)));
        }
    } catch (std::runtime_error &ex) {
        _logger->error("Failed to process connection on descriptor {}: {}", _socket, ex.what());
    }
}

// See Connection.h
void Connection::DoWrite() {
    std::unique_lock<std::mutex> _lock(block_other_connections);
    struct iovec iov[out.size()];
    for (size_t i = 0; i < out.size(); i++) {
        iov[i].iov_base = &out[i][0];
        iov[i].iov_len = out[i].size();
        if (i == 0) {
            iov[0].iov_base = &out[0][0] + written;
        }
    }

    int put_bytes = -1;
    if ((put_bytes = writev(_socket, iov, out.size())) > 0) {
        written += put_bytes;
        size_t i = 0;
        for (; i < out.size(); i++) {
            if (written >= iov[i].iov_len) {
                written -= iov[i].iov_len;
            } else {
                break;
            }
        }
        out.erase(out.begin(), out.begin() + i);
        if (out.empty()) {
            _event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET;
        }
    } else {
        is_alive.store(false);
        throw std::runtime_error("Error in writev()");
    }
}
} // namespace MTnonblock
} // namespace Network
} // namespace Afina
