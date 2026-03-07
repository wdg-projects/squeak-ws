# squeakws

A single-header, minimal-dependency, C++20 implementation of a WebSocket client. Meant for *NIX systems.

## Compiling
You'll need to add OpenSSL to your libraries.

## Usage
I'm not writing documentation yet, the example below should contain all functionality present so far.

```

#include <squeakws.hpp>

#include <iostream>

int main()
{
    try {
        SqueakWS::WebSocket ws("wss://url.goes.here/some/path?to=resource");

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

    } catch (SqueakWS::BaseError &err) {
        std::cerr << "Could not create websocket: " << err.what() << std::endl;
    }
}

// There's more specific error types. Here's the hierarchy:
// BaseError                  Base class for every error thrown directly by the library
//   InitError                Something went wrong while initializing
//   ClosedSocketError        Attempted operation on closed socket
//   ArgumentError            Invalid argument to operation
//   SSLError                 OpenSSL-related fault
//   NetError                 Errors during network operations
//     NameResolutionError    Could not resolve a hostname
//     ConnectionError        Could not connect to peer
//     CommunicationError     Errors during communication between peers
//       EOFError             Unexpected end-of-file
//       ResponseCodeError    WebSocket server gave unexpected HTTP response code (can be inspected with .response_code)

```
