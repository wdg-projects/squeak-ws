#ifndef SQUEAKWS_HPP
#define SQUEAKWS_HPP

#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <netinet/tcp.h>
#include <netdb.h>

#include <unordered_set>
#include <functional>
#include <algorithm>
#include <exception>
#include <cassert>
#include <cstdlib>
#include <format>
#include <thread>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <mutex>
#include <map>
#include <any>

namespace SqueakWS
{
    class BaseError : public std::exception
    {
        std::string err;
    public:
        BaseError(const std::string &err)
            : err{err}
        { }

        const char *what() const noexcept
        {
            return err.c_str();
        }
    };

    class InitError : public BaseError
    {
    public:
        InitError(const std::string &err)
            : BaseError(err)
        { }
    };

    class ClosedSocketError : public BaseError
    {
    public:
        ClosedSocketError(const std::string &err = "Closed socket")
            : BaseError(err)
        { }
    };

    class ArgumentError : public BaseError
    {
    public:
        ArgumentError(const std::string &err)
            : BaseError(err)
        { }
    };

    class SSLError : public BaseError
    {
        public:
        SSLError(const std::string &err)
            : BaseError(err)
        { }
    };

    class NetError : public BaseError
    {
    public:
        NetError(const std::string &err)
            : BaseError{err}
        { }
    };
    
    class NameResolutionError : public NetError
    {
        public:
        NameResolutionError(const std::string &err)
            : NetError(err)
        { }
    };

    class ConnectionError : public NetError
    {
    public:
        ConnectionError(const std::string &err)
            : NetError(err)
        { }
    };
    
    class CommunicationError : public NetError
    {
        public:
        CommunicationError(const std::string &err)
            : NetError(err)
        { }
    };

    class EOFError : public CommunicationError
    {
    public:
        EOFError(const std::string &err = "EOF")
            : CommunicationError(err)
        { }
    };

    class ResponseCodeError : public CommunicationError
    {
    public:
        int response_code;
        ResponseCodeError(const std::string &err, int response_code)
            : CommunicationError(err), response_code(response_code)
        { }
    };

    namespace IMPL
    {
        template<typename T>
        concept CanIncrement = requires (T x, int n) { x += n; };

        template<typename T>
        concept IsBasicIterator = requires (T x) { *x; ++x; };

        template<typename U>
        class IteratorWrapper;

        template<typename U>
        struct IteratorWrapperVTable
        {
            U &(*deref)(const IteratorWrapper<U> *obj);
            void (*inc)(IteratorWrapper<U> *obj);
            void (*incn)(IteratorWrapper<U> *obj, int);
            ssize_t (*sub)(const IteratorWrapper<U> *obj, const IteratorWrapper<U>&);
            bool (*eq)(const IteratorWrapper<U> *obj, const IteratorWrapper<U>&);

            template<IsBasicIterator T>
            IteratorWrapperVTable(T*)
            {
                deref = [](const IteratorWrapper<U> *obj) -> U& {
                    return (U&) **std::any_cast<T>(&obj->iter);
                };
                inc = [](IteratorWrapper<U> *obj) -> void {
                    ++*std::any_cast<T>(&obj->iter);
                };
                incn = [](IteratorWrapper<U> *obj, int n) -> void {
                    if constexpr (CanIncrement<T>) {
                        *std::any_cast<T>(&obj->iter) += n;
                    } else {
                        for (int i = 0; i < n; ++i)
                            obj->inc();
                    }
                };
                sub = [](const IteratorWrapper<U> *obj, const IteratorWrapper<U> &other) -> ssize_t {
                    // TODO: Check if the other iterator is of the same kind
                    const T &lhs = *std::any_cast<T>(&other.iter);
                    const T &rhs = *std::any_cast<T>(&obj->iter);
                    return rhs - lhs;
                };
                eq = [](const IteratorWrapper<U> *obj, const IteratorWrapper<U> &other) -> bool {
                    // TODO: Check if the other iterator is of the same kind
                    return *std::any_cast<T>(&obj->iter) == *std::any_cast<T>(&other.iter);
                };
            }
        };

        template<typename U>
        class IteratorWrapper
        {
            friend IteratorWrapperVTable<U>;

            std::any iter;
            const IteratorWrapperVTable<U> *vt;

        public:
            template<typename V, typename T = std::decay_t<V>>
            IteratorWrapper(V &&i) requires (IsBasicIterator<T> && !std::is_same_v<T, IteratorWrapper<U>>)
                : iter(std::move(i))
            {
                static const IteratorWrapperVTable<U> VT{(T*)nullptr};
                vt = &VT;
            }
        
            IteratorWrapper(const IteratorWrapper<U> &other)
                : iter(other.iter), vt(other.vt)
            { }
        
            IteratorWrapper<U> &operator=(const IteratorWrapper<U> &other)
            {
                iter = other.iter;
                vt = other.vt;
                return *this;
            }
        
            IteratorWrapper(IteratorWrapper<U> &&other)
                : iter(std::move(other.iter)), vt(other.vt)
            { }
        
            U &operator*() const
            {
                return vt->deref(this);
            }
        
            bool operator==(const IteratorWrapper<U> &other) const
            {
                return vt->eq(this, other);
            }
        
            IteratorWrapper<U> &operator++()
            {
                vt->inc(*this);
                return *this;
            }
        
            ssize_t operator-(IteratorWrapper<U> &other) const
            {
                return vt->sub(this, other);
            }
        
            template<typename T>
            T as()
            {
                return std::any_cast<T>(iter);
            }

            IteratorWrapper<U> &operator+=(int n)
            {
                vt->incn(this, n);
                return *this;
            }
        };
        
        using CharIteratorWrapper = IteratorWrapper<char>;
        using ConstCharIteratorWrapper = IteratorWrapper<const char>;
        
        struct MemoryBIO
        {
            BIO *bio;
        
            inline MemoryBIO()
                : bio{BIO_new(BIO_s_mem())}
            {
                if (!bio)
                    throw std::bad_alloc();
            }
        
            inline ~MemoryBIO()
            {
                BIO_free(bio);
            }
        
            inline std::string collect()
            {
                std::string res;
                collect(res);
                return res;
            }
        
            inline void collect(std::string &into)
            {
                const int BUFSIZE = 1024;
        
                BIO_seek(bio, 0);
                int read;
                std::unique_ptr<char[]> buf = std::make_unique<char[]>(BUFSIZE);
                do {
                    read = BIO_read(bio, &buf[0], BUFSIZE);
                    into.append({ &buf[0], (size_t)read });
                } while (read == BUFSIZE);
            }
        };

        class StreamSocket
        {
        public:
            virtual int fd() = 0;
            virtual CharIteratorWrapper read(CharIteratorWrapper begin, CharIteratorWrapper end) = 0;
            virtual ConstCharIteratorWrapper write(ConstCharIteratorWrapper begin, ConstCharIteratorWrapper end) = 0;

            inline void read_all(CharIteratorWrapper begin, CharIteratorWrapper end)
            {
                while (begin != end) {
                    auto iter = read(begin, end);
                    if (iter == begin)
                        throw EOFError();
                    begin = iter;
                }
            }

            inline void write_all(ConstCharIteratorWrapper begin, ConstCharIteratorWrapper end)
            {
                while (begin != end) {
                    auto iter = write(begin, end);
                    if (iter - begin == 0)
                        throw EOFError();
                    begin = iter;
                }
            }

            inline std::string read_until(const std::string &delim, int limit = -1)
            {
                std::string buf;
                char c;
                while (!buf.ends_with(delim)) {
                    if (read(&c, &c + 1) != &c + 1)
                        throw EOFError();
                    if (limit == -1 || buf.size() < limit + 2) {
                        buf.push_back(c);
                    } else {
                        buf[buf.size() - 2] = buf[buf.size() - 1];
                        buf[buf.size() - 1] = c;
                    }
                }
                return buf.substr(0, buf.size() - 2);
            }
        };

        inline int init_openssl_done = 0;

        inline void init_openssl()
        {
            if (init_openssl_done < 1) {
                OpenSSL_add_all_algorithms();
                ERR_load_crypto_strings();
                SSL_load_error_strings();
                init_openssl_done = 1;
            }
            if (init_openssl_done < 2) {
                if (SSL_library_init() < 0)
                    throw InitError("Failed to initialize OpenSSL");
                init_openssl_done = 2;
            }
        }
        
        class _DefaultHTTPSSSLContextGuard
        {
            mutable SSL_CTX *ctx = nullptr;
        public:
            inline ~_DefaultHTTPSSSLContextGuard()
            {
                SSL_CTX_free(ctx);
            }
        
            inline SSL_CTX *const &operator*() const
            {
                init_openssl();
        
                if (!(ctx = SSL_CTX_new(TLS_client_method())))
                    throw InitError("Failed to create OpenSSL context");
        
                SSL_CTX_set_options(ctx, SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2);  // FIXME: dubious
                SSL_CTX_set_default_verify_paths(ctx);
        
                return ctx;
            }
        } inline default_https_ssl_context;
        
        class TCPSocket : public StreamSocket
        {
            int sockfd = 0;
        public:
            inline TCPSocket(std::string hostname, int port)
            {
                struct hostent *host;
                if (!(host = gethostbyname(hostname.c_str())))    // FIXME: Don't use gethostbyname
                    throw NameResolutionError(std::format("Could not resolve {}: {}", hostname, strerror(errno)));
        
                sockfd = socket(AF_INET, SOCK_STREAM, 0);
        
                struct sockaddr_in dest_addr;
                dest_addr.sin_family = AF_INET;
                dest_addr.sin_port = htons(port);
                dest_addr.sin_addr.s_addr = *(long*)(host->h_addr);
                memset(&dest_addr.sin_zero, 0, sizeof(dest_addr.sin_zero));
        
                char saddr[64];
                strncpy(saddr, inet_ntoa(dest_addr.sin_addr), 64);
        
                int value = 1;
                setsockopt(sockfd, SOL_TCP, TCP_NODELAY, &value, sizeof(int));
        
                if (connect(sockfd, (struct sockaddr *) &dest_addr, sizeof(struct sockaddr)) < 0)
                    throw ConnectionError(std::format("Could not connect to https://{} ({}:{}): {}", saddr, port, hostname, strerror(errno)));
            }
        
            inline ~TCPSocket()
            {
                if (sockfd)
                    close(sockfd);
            }
        
            inline TCPSocket(const TCPSocket &other) = delete;
        
            inline TCPSocket(TCPSocket &&other)
                : sockfd(other.sockfd)
            {
                other.sockfd = 0;
            }
        
            inline CharIteratorWrapper read(CharIteratorWrapper begin, CharIteratorWrapper end) override
            {
                if (!sockfd)
                    throw ClosedSocketError();
                return begin += ::read(sockfd, &*begin, end - begin);
            }
        
            inline ConstCharIteratorWrapper write(ConstCharIteratorWrapper begin, ConstCharIteratorWrapper end) override
            {
                if (!sockfd)
                    throw ClosedSocketError();
                return begin += ::write(sockfd, &*begin, end - begin);
            }
        
            inline int fd() override
            {
                if (!sockfd)
                    throw ClosedSocketError();
                return sockfd;
            }
        };
        
        class TLSSocket : public StreamSocket
        {
            SSL_CTX *ctx;
            SSL *ssl;
        public:
            inline TLSSocket(SSL_CTX *ctx, std::string hostname, int port)
                : ctx{ctx}, ssl{SSL_new(ctx)}
            {
                if (!ssl) {
                    MemoryBIO bio;
                    ERR_print_errors(bio.bio);
                    throw SSLError(std::format("Could not create SSL object:\n{}", bio.collect()));
                }
        
                if (!X509_VERIFY_PARAM_set1_host(SSL_get0_param(ssl), hostname.data(), hostname.size())) {
                    MemoryBIO bio;
                    ERR_print_errors(bio.bio);
                    throw SSLError(std::format("Could not enable hostname verification:\n{}", bio.collect()));
                }
        
                SSL_set_verify(ssl, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
                SSL_set_tlsext_host_name(ssl, hostname.c_str());
        
                struct hostent *host;
                if (!(host = gethostbyname(hostname.c_str())))    // TODO: Don't use gethostbyname
                    throw NameResolutionError(std::format("Could not resolve {}: {}", hostname, strerror(errno)));
        
                int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        
                struct sockaddr_in dest_addr;
                dest_addr.sin_family = AF_INET;
                dest_addr.sin_port = htons(port);
                dest_addr.sin_addr.s_addr = *(long*)(host->h_addr);
                memset(&dest_addr.sin_zero, 0, sizeof(dest_addr.sin_zero));
        
                char saddr[64];
                strncpy(saddr, inet_ntoa(dest_addr.sin_addr), 64);
        
                int value = 1;
                setsockopt(sockfd, SOL_TCP, TCP_NODELAY, &value, sizeof(int));
        
                if (connect(sockfd, (struct sockaddr *) &dest_addr, sizeof(struct sockaddr)) < 0)
                    throw ConnectionError(std::format("Could not connect to https://{} ({}:{}): {}", saddr, port, hostname, strerror(errno)));

                SSL_set_fd(ssl, sockfd);
        
                if (SSL_connect(ssl) <= 0) {
                    MemoryBIO bio;
                    ERR_print_errors(bio.bio);
                    throw SSLError(std::format("Could not connect (via TLS) to https://{} ({}:{}):\n{}", saddr, port, hostname, bio.collect()));
                }
            }
        
            inline ~TLSSocket()
            {
                if (ssl) {
                    SSL_shutdown(ssl);
                    if (int fd = SSL_get_fd(ssl))
                        close(fd);
                    SSL_free(ssl);
                }
            }
        
            inline TLSSocket(const TLSSocket &other) = delete;
        
            inline TLSSocket(TLSSocket &&other)
                : ctx(other.ctx), ssl(other.ssl)
            {
                other.ctx = nullptr;
                other.ssl = nullptr;
            }
        
            inline CharIteratorWrapper read(CharIteratorWrapper begin, CharIteratorWrapper end) override
            {
                if (!ssl)
                    throw ClosedSocketError();
                return begin += SSL_read(ssl, &*begin, end - begin);
            }
        
            inline ConstCharIteratorWrapper write(ConstCharIteratorWrapper begin, ConstCharIteratorWrapper end) override
            {
                if (!ssl)
                    throw ClosedSocketError();
                return begin += SSL_write(ssl, &*begin, end - begin);
            }
        
            inline int fd() override
            {
                if (!ssl)
                    throw ClosedSocketError();
                return SSL_get_fd(ssl);
            }
        };
        
        struct URL
        {
            std::string protocol;
            std::string hostname;
            int port;
            std::string path;
        
            inline URL(std::string url)
                : URL(url, { })
            { }
        
            inline URL(std::string url, std::unordered_set<std::string> require_protocol)
            {
                int pos = url.find("://");
                protocol = pos == std::string::npos ? "" : url.substr(0, pos);
                
                if (require_protocol.size() && !require_protocol.contains(protocol)) {
                    std::string s;
                    int i = 0;
                    for (const auto &entry : require_protocol) {
                        s += entry;
                        if (i < require_protocol.size() - 2)
                            s += ", ";
                        else if (i == require_protocol.size() - 2 && require_protocol.size() > 2)
                            s += ", or ";
                        else if (i == require_protocol.size() - 2 && require_protocol.size() == 2)
                            s += " or ";
                        ++i;
                    }
                    throw ArgumentError(std::format("Protocol must be one of {}; got '{}'", s, protocol));
                }
                if (pos != std::string::npos)
                    url = url.substr(pos + 3);
        
                pos = url.find("/");
                if (pos == std::string::npos)
                    pos = url.size();
                std::string hostname_port = url.substr(0, pos);
                if (int colon_pos = hostname_port.find(":"); colon_pos != std::string::npos) {
                    hostname = hostname_port.substr(0, colon_pos);
                    port = std::stoi(hostname_port.substr(colon_pos + 1));
                } else {
                    hostname = hostname_port;
                    port = 0;
                }
                path = url.substr(pos);
            }
        };
        
        inline std::string as_base64(std::string text)
        {
            const char *const B64ALPHA = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        
            std::string result;
            result.reserve((text.size() + 2) / 3 * 4);
            unsigned i;
            for (i = 0; i < text.size(); i += 3) {
                uint32_t v = ((uint32_t)(uint8_t)text[i] << 16ul);
                if (i + 1 < text.size())
                    v |= ((uint32_t)(uint8_t)text[i+1] << 8ul);
                if (i + 2 < text.size())
                    v |= (uint32_t)(uint8_t)text[i+2];
                assert(v <= 0xFF'FFFF);
                result.push_back(B64ALPHA[v >> 18]);
                result.push_back(B64ALPHA[(v >> 12) & 0x3f]);
                result.push_back(B64ALPHA[(v >> 6) & 0x3f]);
                result.push_back(B64ALPHA[v & 0x3f]);
            }
            int to_fill = i - text.size();
            for (int j = 0; j < to_fill; ++j)
                result.pop_back();
            for (int j = 0; j < to_fill; ++j)
                result.push_back('=');
        
            return result;
        }

        template<typename T>
        concept IsStreamSocket = std::is_base_of_v<StreamSocket, T>;
        
        template<IsStreamSocket T>
        class HTTPHeadersManager
        {
            T *sock;
        
            std::string rqmethod;
            std::string rqpath;
            // std::vector<std::pair<std::string, std::string>> rqheaders;
            std::map<std::string, std::string> rqheaders;
        
            bool rpavail = false;
            int rpcode;
            std::vector<std::pair<std::string, std::string>> rpheaders;
        
            void set_method(const std::string &method, const std::string &path)
            {
                if (rpavail)
                    throw ArgumentError("Request already sent");
                if (rqpath.size())
                    throw ArgumentError("Can only set 1 method type");
                rqmethod = method;
                rqpath = path;
            }
        
        public:
            inline HTTPHeadersManager(T &connection)
                : sock{&connection}
            { }
        
            inline void get(const std::string &path)
            {
                set_method("GET", path);
            }
        
            inline void header(const std::string &key, const std::string &value)
            {
                // rqheaders.emplace_back(key, value);
                rqheaders[key] = value;
            }
        
            inline int response_code() const
            {
                if (!rpavail)
                    throw ArgumentError("No response received yet; invoke .fulfil()");
                return rpcode;
            }
        
            inline const std::vector<std::pair<std::string, std::string>> &response_headers() const
            {
                if (!rpavail)
                    throw ArgumentError("No response received yet; invoke .fulfil()");
                return rpheaders;
            }
        
            inline void fulfil(std::string payload = {})
            {
                if (rpavail)
                    throw ArgumentError("Request already sent");
        
                const int HEADER_LIMIT = 256;
        
                auto request = std::format("GET {} HTTP/1.1\r\n", rqpath);
                for (auto header : rqheaders) {
                    request.append(header.first);
                    request.append(": ");
                    request.append(header.second);
                    request.append("\r\n");
                }
                request.append("\r\n");
                // std::cout << request << std::endl;
        
                sock->write_all(request.begin(), request.end());
                sock->write_all(payload.begin(), payload.end());
        
                auto rsp = sock->read_until("\r\n", 64);
                if (!rsp.starts_with("HTTP/1.1 "))
                    throw CommunicationError("Invalid server response (not an HTTP/HTTPS server?)");
                rsp = rsp.substr(9);
                auto pos = rsp.find(" ");
                rpcode = std::stoi(rsp.substr(0, pos));
        
                for (int headeri = 0; headeri < HEADER_LIMIT; ++headeri) {
                    auto header = sock->read_until("\r\n", 256);
                    if (!header.size())
                        break;
        
                    auto pos = header.find(":");
                    if (pos == std::string::npos)
                        throw CommunicationError("Invalid server response (not an HTTP/HTTPS server?)");

                    std::string key = header.substr(0, pos);
                    for (auto &c : key)
                        c = std::tolower(c);
        
                    std::string value = header.substr(pos + 1);
                    for (pos = 0; pos < value.size(); ++pos)
                        if (value[pos] != ' ')
                            break;
                    value = value.substr(pos);
        
                    rpheaders.emplace_back(key, value);
                }
        
                rpavail = true;
            }
        };
    }

    inline void openssl_initialized()
    {
        IMPL::init_openssl_done = 2;
    }

    struct WebSocketConfig
    {
        std::vector<std::pair<std::string, std::string>> headers = {};
        size_t payload_size_limit = 0x4'000'000;
        size_t msg_size_limit = 0x100'000;
    };

    class WebSocket
    {
        IMPL::URL wsurl;
        std::unique_ptr<IMPL::StreamSocket> lower;
        size_t payload_size_limit;
        size_t msg_size_limit;
        std::recursive_mutex rwmutex;
        std::function<void(const std::string&, bool)> on_message_cb;
        std::function<void(uint16_t)> on_close_cb;
        std::unique_ptr<std::thread> poll_recv_thread;

        enum class PacketKind { CLOSE = 0x8, PING = 0x9, BINARY = 0x2, UTF8 = 0x1, INVALID = 0x0 };

        inline void receive_packet(PacketKind &kind, std::string &msg)
        {
            msg = {};

            bool fin, first = true;
            {
                uint8_t head[2];
                lower->read_all((char*)&head, (char*)&head + 2);
                uint8_t payload_len_raw[8] = { 0 };

                bool mask = (head[1] >> 7) & 1;
                fin = (head[0] >> 7) & 1;
                uint8_t opcode = head[0] & 15;

                if ((opcode >= 0x3 && opcode <= 0x7) || opcode >= 0xB)
                    throw CommunicationError("Corrupted frame (invalid opcode)");
                
                if (first)
                    kind = PacketKind(opcode);
                else if (opcode != 0x0)
                    throw CommunicationError("Corrupted frame (continuation frame must have continuation opcode)");

                uint64_t payload_length = head[1] & 0x7f;
                if (payload_length == 126) {
                    lower->read_all((char*)&payload_len_raw, (char*)&payload_len_raw + 8);
                    payload_length = ((uint16_t)payload_len_raw[0] << 8u) | (uint16_t)payload_len_raw[1];
                } else if (payload_length == 127) {
                    lower->read_all((char*)&payload_len_raw, (char*)&payload_len_raw + 8);
                    payload_length = (uint64_t)payload_len_raw[7]
                        | ((uint64_t)payload_len_raw[6] << 8u)
                        | ((uint64_t)payload_len_raw[5] << 16u)
                        | ((uint64_t)payload_len_raw[4] << 24u)
                        | ((uint64_t)payload_len_raw[3] << 32u)
                        | ((uint64_t)payload_len_raw[2] << 40u)
                        | ((uint64_t)payload_len_raw[1] << 48u)
                        | ((uint64_t)payload_len_raw[0] << 56u);
                }

                if (msg.size() + payload_length > msg_size_limit)
                    throw CommunicationError(std::format("Receiving message at least {} bytes long, over the set limit of {} bytes", msg.size() + payload_length, msg_size_limit));

                if (payload_length >= payload_size_limit)
                    throw CommunicationError(std::format("Receiving frame {} bytes long, over the set limit of {} bytes", payload_length, payload_size_limit));

                uint8_t masking_key[4] = { 0 };
                if (mask)
                    lower->read_all((char*)&masking_key, (char*)&masking_key + 4);

                std::string payload;
                payload.resize(payload_length);
                lower->read_all(payload.begin(), payload.end());
                if (mask)
                    for (int i = 0; i < payload.size(); ++i)
                        payload[i] = (char)(masking_key[i & 3] ^ (uint8_t)payload[i]);

                msg.append(payload);

                first = false;
            } while (!fin);
        }

        inline void send_packet(uint8_t opcode, const std::string &msg)
        {
            const uint64_t MAX_PAYLOAD = 1024ULL;

            for (int i = 0; i < msg.size(); i += MAX_PAYLOAD) {
                bool fin = i + MAX_PAYLOAD >= msg.size();

                uint64_t payload_size = fin ? msg.size() - i : MAX_PAYLOAD;

                uint8_t head[14] = { 0 };
                head[0] = opcode & 15u | ((uint8_t)fin << 7u);
                if (payload_size > 65535ull) {
                    head[1] = 127 | 128;
                    head[2] = (payload_size >> 56u);
                    head[3] = (payload_size >> 48u) & 0xff;
                    head[4] = (payload_size >> 40u) & 0xff;
                    head[5] = (payload_size >> 32u) & 0xff;
                    head[6] = (payload_size >> 24u) & 0xff;
                    head[7] = (payload_size >> 16u) & 0xff;
                    head[8] = (payload_size >> 8u) & 0xff;
                    head[9] = payload_size & 0xff;
                    memset(&head[10], 0, 4);
                    lower->write_all((char*)&head, (char*)&head + 14);

                } else if (payload_size > 126ull) {
                    head[1] = 126 | 128;
                    head[2] = payload_size >> 8u;
                    head[3] = payload_size & 0xff;
                    memset(&head[4], 0, 4);
                    lower->write_all((char*)&head, (char*)&head + 8);

                } else {
                    head[1] = payload_size | 128;
                    memset(&head[2], 0, 4);
                    lower->write_all((char*)&head, (char*)&head + 6);
                }

                lower->write_all(msg.begin() + i, msg.begin() + i + payload_size);
            }
        }

        inline void target_poll_recv()
        {
            struct pollfd pfds[] = { { lower->fd(), POLLIN, 0 } };
            while (true) {
                if (poll(pfds, sizeof(pfds)/sizeof(pfds[0]), -1) < 0)
                    throw CommunicationError(std::format("Polling socket: {}", strerror(errno)));

                if (pfds[0].revents & POLLIN) {
                    PacketKind kind;
                    std::string msg;
                    {
                        std::scoped_lock lock{rwmutex};
                        receive_packet(kind, msg);
                        switch (kind) {
                            case PacketKind::CLOSE:
                                send_packet(0x8, msg);
                                break;
                            case PacketKind::PING:
                                send_packet(0xA, msg);
                                break;
                            case PacketKind::BINARY:
                            case PacketKind::UTF8:
                                break;
                            case PacketKind::INVALID:
                                assert(false);
                        }
                    }

                    switch (kind) {
                        case PacketKind::CLOSE:
                            if (on_close_cb)
                                on_close_cb((uint16_t)msg[0] << 8u | (uint16_t)msg[1]);
                            break;
                        case PacketKind::BINARY:
                            if (on_message_cb)
                                on_message_cb(msg, false);
                            break;
                        case PacketKind::UTF8:
                            if (on_message_cb)
                                on_message_cb(msg, true);
                            break;
                        default:
                            break;
                    }

                } else {
                    break;
                }
            }
        }

    public:
        inline WebSocket(std::string url, WebSocketConfig cfg = {})
            : WebSocket(*IMPL::default_https_ssl_context, url, cfg)
        { }

        inline WebSocket(SSL_CTX *ctx, std::string url, WebSocketConfig cfg = {})
            : wsurl{url, { "ws", "wss" }},
            payload_size_limit{cfg.payload_size_limit},
            msg_size_limit{cfg.msg_size_limit}
        {
            if (wsurl.port == 0)
                wsurl.port = wsurl.protocol == "wss" ? 443 : 80;

            if (wsurl.protocol == "wss")
                lower = std::make_unique<IMPL::TLSSocket>(ctx, wsurl.hostname, wsurl.port);
            else
                lower = std::make_unique<IMPL::TCPSocket>(wsurl.hostname, wsurl.port);

            std::string key;
            for (int i = 0; i < 16; ++i)
                key.push_back((char)(rand() & 0xff));  // it doesn't have to be GOOD randomness
            std::string keyb64 = IMPL::as_base64(key);

            std::string keyhash;
            {
                std::string tmp = keyb64 + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
                keyhash = { (char*)SHA1((uint8_t*)tmp.data(), tmp.size(), NULL), 20 };
            }
            std::string keyhashb64 = IMPL::as_base64(keyhash);

            IMPL::HTTPHeadersManager http(*lower);
            http.get(wsurl.path);
            http.header("Host", wsurl.hostname);
            http.header("Connection", "upgrade");
            http.header("Upgrade", "websocket");
            http.header("Sec-WebSocket-Key", keyb64);
            http.header("Sec-WebSocket-Version", "13");
            http.header("Accept", "*/*");
            http.header("Accept-Language", "en-US,en;q=0.9");
            for (const auto &header : cfg.headers)
                http.header(header.first, header.second);
            http.fulfil();

            if (http.response_code() != 101)
                throw ResponseCodeError(std::format("Got response code {}, expected 101", http.response_code()), http.response_code());

            bool got_upgrade = false, got_connection = false, got_challenge = false;
            for (const auto &header : http.response_headers()) {
                std::string key = header.first, value = header.second, value_lower = header.second;
                for (auto &c : value_lower)
                    c = std::tolower(c);

                if (key == "upgrade") {
                    if (value_lower != "websocket")
                        throw CommunicationError(std::format("Invalid server response (upgrade header is '{}', should be 'websocket'; not a websocket endpoint?)", value));
                    got_upgrade = true;

                } else if (key == "connection") {
                    if (value_lower != "upgrade")
                        throw CommunicationError(std::format("Invalid server response (connection header is '{}', should be 'upgrade'; not a websocket endpoint?)", value));
                    got_connection = true;

                } else if (key == "sec-websocket-accept") {
                    if (value != keyhashb64)
                        throw CommunicationError(std::format("Invalid server response (challenge failed; expected {}, got {})", keyhashb64, value));
                    got_challenge = true;
                }
            }

            if (!got_upgrade || !got_connection || !got_challenge)
                throw CommunicationError("Invalid server response (missing response headers; not a websocket endpoint?)");

            poll_recv_thread = std::make_unique<std::thread>([this]() { target_poll_recv(); });
            poll_recv_thread->detach();
        }

        inline ~WebSocket()
        {
            close(1000);
        }

        inline void close(uint16_t code)
        {
            std::scoped_lock lock{rwmutex};
            std::string msg;
            msg.push_back((char)(uint8_t)((uint16_t)code >> 8u));
            msg.push_back((char)(uint8_t)(uint16_t)code);
            send_packet(0x8, msg);
        }

        inline void send_text(std::string msg)
        {
            std::scoped_lock lock{rwmutex};
            send_packet(0x1, msg);
        }

        inline void send_binary(std::string msg)
        {
            std::scoped_lock lock{rwmutex};
            send_packet(0x2, msg);
        }

        inline void on_message(std::function<void(const std::string&, bool)> f)
        {
            on_message_cb = f;
        }

        inline void on_close(std::function<void(uint16_t)> f)
        {
            on_close_cb = f;
        }
    };
}

#endif  // SQUEAKWS_HPP
