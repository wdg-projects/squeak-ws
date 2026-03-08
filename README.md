# squeakws

A single-header, minimal-dependency, C++20 implementation of a WebSocket client. Meant for *NIX systems.

## Compiling
You'll need to add OpenSSL to your libraries.

## Usage
Rudimentary documentation can be generated with `doxygen`. The example below contains the most common functionality:

```cpp

#include <squeakws.hpp>

#include <iostream>
#include <chrono>

int main()
{
    try {
        SqueakWS::WebSocket ws("wss://url.goes.here/some/path?to=resource", {
            .headers = {
                { "X-Look-You-Can", "add custom headers" }
            },
            // These two are actually the default values
            .payload_size_limit = 0x100'000,
            .msg_size_limit = 0x4'000'000
        });

        ws.on_message([](const std::string &msg, bool is_binary)
        {
            std::cout << (is_binary ? "Binary: " : "UTF-8 Text: ")
                      << msg << std::endl;
        });

        ws.on_close([](uint16_t code)
        {
            std::cout << "Connection closed, server gave reason " << code << std::endl;
        });

        ws.send_text("Hii");
        ws.send_binary(std::string("\1\2\3", 3));

        ws.run();

    } catch (SqueakWS::BaseError &err) {
        std::cerr << "Could not create websocket: " << err.what() << std::endl;
    }
}

```
