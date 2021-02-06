#include <iostream>
#include <string>
#include <chrono>
#include <deque>
#include <random>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/bind.hpp>

#include "nlohmann/json.hpp"
#include "magic_enum/magic_enum.hpp"

namespace ba = boost::asio;
using ba::ip::tcp;
using json = nlohmann::json;
using namespace std::chrono_literals;

enum DeviceType {
    EXT_CLIENT, //identifier for an external client
    EXT_SERVER, //identifier for an external server
    ME // personal identifier
};

struct Message {
    bool isReady;
    enum DeviceType device_client;
    json message;
};

struct SharedBuffers {
    boost::asio::streambuf  server_buffer;
    boost::asio::streambuf  client_buffer;
    std::deque<boost::asio::ip::tcp::socket> sessions;
};

static auto const now = &std::chrono::steady_clock::now;
static auto const start = now();

auto logger(std::string name) {
    return [name](auto const&... args) {
        ((std::cout << "at" << std::setw(6) << (now() - start)/1ms << "ms\t"
                    << name << "\t") 
            << ... << args) << std::endl;
    };
}

Message formatMessage(const DeviceType deviceClient, const std::string message) {
    json messageJSON;
    messageJSON["data"] = message;
    messageJSON["id"] = magic_enum::enum_name<DeviceType>(DeviceType::ME).data(); // SENDER

    Message oMessage;
    oMessage.device_client = deviceClient;
    oMessage.message = messageJSON;
    oMessage.isReady = true;
    return oMessage;
}

Message handleMessage(const std::string bufferStr) {
    std::uniform_int_distribution<int> d(0, 1);
    std::random_device rd1;

    Message output_message;
    output_message.isReady = false;
    auto input_message = json::parse(bufferStr);
    auto device = magic_enum::enum_cast<DeviceType>(input_message["id"].get<std::string>());
    if (device.has_value()) {
        auto src_device = device.value();
        auto src_message = input_message["data"];
        // Do some processing with src_device and src_message and send to EXT_CLIENT or EXT_SERVER
    	auto deviceClientStr = std::to_string(d(rd1)%2);
        auto output_device = magic_enum::enum_cast<DeviceType>(deviceClientStr);
        if (output_device.has_value()) {
            output_message = formatMessage(output_device.value(), "HANDLE_MESSAGE_REPLY");
        }
    }
    return output_message;
}

class Server {
public:
    Server(ba::io_service &io, int port, SharedBuffers &buffers) {
        spawn(io, [&io, port, &buffers] (ba::yield_context yc) {
            auto log_s = logger("accept");
            tcp::acceptor acc(io, {{}, port});
            acc.set_option(tcp::acceptor::reuse_address(true));

            while (true) {
                tcp::socket& s = buffers.sessions.emplace_back(io);
                acc.async_accept(s, yc);
                log_s("accepted client on port: ", port);

                // Simulate  a new process to read/write
                auto log=logger("server: session #" + std::to_string(buffers.sessions.size()));
                spawn(yc, [&s, &buffers, log] (ba::yield_context yc) mutable {
                    log("Connection from ", s.remote_endpoint());

                    while (true) {
                        std::string response;
                        async_read_until(s, ba::dynamic_buffer(response), "\n", yc);

                        if (!response.empty()) {
                            log("data received\n:", response, "**********\n");
                            Message oMessage = handleMessage(response);
                            if (oMessage.isReady) {
                                log("reply to client");
                                if (oMessage.device_client == DeviceType::EXT_SERVER) {
                                    // Reply through the client 
                                    std::cout << "Fill in client_buffer" << std::endl;
                                    std::ostream request_stream(&buffers.client_buffer);
                                    request_stream << oMessage.message.dump(4) << "\n" << std::flush;

                                    // std::string strBuff(boost::asio::buffers_begin(buffers.client_buffer.data()),
                                    //                 boost::asio::buffers_begin(buffers.client_buffer.data()) + buffers.client_buffer.size());                                
                                    // log(strBuff);
                                } else {
                                    // Reply to the same client
                                    async_write(s, ba::buffer(oMessage.message.dump(4)), yc);
                                }
                            }
                        }
                    }
                });

                spawn(yc, [&io, &yc, &s, &buffers, &log] (ba::yield_context yc_) mutable {
                    while (true) {
                        ba::steady_timer(io, 50ms).async_wait(yc);
                        if (buffers.server_buffer.size() > 0) {
                            async_write(s, buffers.server_buffer.data(), yc);
                            buffers.server_buffer.consume(buffers.server_buffer.size()); // Clear buffer
                        }
                    }
                });

            }
        });
    }
    ~Server() { std::cout << "Destroy Server" << std::endl;}
};

class Client {
public:
    Client(ba::io_context& io, const std::string client_ip, int client_port, SharedBuffers &buffers) {
        spawn(io, [&io, &buffers, client_ip, client_port, this, log=logger("client")]
            (ba::yield_context yc) {
                tcp::resolver r(io);
                tcp::socket s(io);

                async_connect(s, r.async_resolve(client_ip, std::to_string(client_port), yc),yc);
                spawn(yc, [&s, &buffers, &log] (ba::yield_context yc) mutable {
                    while (true) {
                        std::string response;
                        async_read_until(s, ba::dynamic_buffer(response), "\n", yc);
                        if (!response.empty()) {
                            log("data received\n:", response, "**********\n");
                            Message oMessage = handleMessage(response);
                            if (oMessage.isReady) {
                                if (oMessage.device_client == DeviceType::EXT_SERVER) {
                                    // Client reply
                                    log("client reply");
                                    async_write(s, ba::buffer(oMessage.message.dump(4)), yc);
                                } else {
                                    // Server reply -> Fill in server_buffer
                                    log("send to server to reply to another client");
                                    std::ostream request_stream(&buffers.server_buffer);
                                    request_stream << oMessage.message.dump(4) << std::flush;
                                }
                            }
                        }
                    }
                });

                spawn(yc, [&io, &yc, &s, &buffers, &log] (ba::yield_context yc_) mutable {
                    while (true) {
                        /* It doesn't work. In only works if I set:
                        ba::steady_timer(io, 50ms).async_wait(yc);
                        and then do the logic), but then I cannot define another timer with another execution.
                        Ideally I would prefer to trigger an async_write as soon as buffer has data */

                        ba::steady_timer(io, 50ms).async_wait([&](const boost::system::error_code& ec) {
                            // Check if server added some messages to be transferred through the client
                            if (buffers.client_buffer.size() > 0) {
                                async_write(s, buffers.client_buffer.data(), yc);
                                buffers.client_buffer.consume(buffers.client_buffer.size()); // Clear buffer
                            }
                        });

                        ba::steady_timer(io, 3s).async_wait([&](const boost::system::error_code& ec) {
                            // Send periodic message
                            Message oMessage = formatMessage(DeviceType::EXT_SERVER, "KEEP_ALIVE");
                            if (oMessage.isReady) {
                                log("send KEEP_ALIVE");
                                async_write(s, ba::buffer(oMessage.message.dump(4)), yc);
                            }
                        });
                    }
                });
            });
    }
    ~Client() { std::cout << "Destroy Client" << std::endl; }
};

int main(int argc, char *argv[]) {
    int serverPort = 28001;
    int acqPort = 28002;
    std::string acqIp = "172.17.0.1";

    try {
        // Shared resources
        boost::asio::io_service io;
        struct SharedBuffers buffers;

        //Initialize client and server
        Server(io, serverPort, buffers);
        Client(io, acqIp, acqPort, buffers);

        io.run();
    } catch (std::exception& e) {
        std::cout << "Exception: " << e.what() << "\n";
    }
}