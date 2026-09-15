// On-demand PS2 cover art downloader. SPDX-License-Identifier: GPL-3.0-or-later
#include "cover_fetch.hpp"

#include <fcntl.h>
#include <unistd.h>

extern "C" {
int sceNetInit();
int sceNetPoolCreate(const char*, int, int);
int sceNetPoolDestroy(int);
int sceSslInit(std::size_t);
int sceSslTerm(int);
int sceHttp2Init(int, int, std::size_t, int);
int sceHttp2Term(int);
int sceHttp2CreateTemplate(int, const char*, int, int);
int sceHttp2DeleteTemplate(int);
int sceHttp2CreateRequestWithURL(int, const char*, const char*, std::uint64_t);
int sceHttp2DeleteRequest(int);
int sceHttp2SendRequest(int, const void*, std::size_t);
int sceHttp2GetStatusCode(int, int*);
int sceHttp2ReadData(int, void*, std::size_t);
int sceHttp2SetResolveTimeOut(int, unsigned int);
int sceHttp2SetConnectTimeOut(int, unsigned int);
int sceHttp2SetSendTimeOut(int, unsigned int);
int sceHttp2SetRecvTimeOut(int, unsigned int);
int sceHttp2SetTimeOut(int, unsigned int);
int sceHttp2SetAutoRedirect(int, int);
}

namespace cover_fetch {
namespace {
constexpr char agent[] = "ps2-library/1.0";
constexpr char debug_path[] = "/data/PS2/cover-debug.log";
bool net_ready = false;

char *append(char *p, char *end, std::string_view text) noexcept {
    for (const char c : text)
        if (p < end)
            *p++ = c;
    return p;
}
char *append_unsigned(char *p, char *end, unsigned long value) noexcept {
    char reversed[24]{};
    std::size_t length = 0;
    do {
        reversed[length++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0 && length < sizeof(reversed));
    while (length > 0 && p < end)
        *p++ = reversed[--length];
    return p;
}
char *append_hex(char *p, char *end, unsigned long value) noexcept {
    constexpr char digits[] = "0123456789ABCDEF";
    p = append(p, end, "0x");
    bool started = false;
    for (int shift = 28; shift >= 0; shift -= 4) {
        const unsigned nibble = static_cast<unsigned>((value >> shift) & 0xF);
        if (nibble != 0 || started || shift == 0) {
            started = true;
            if (p < end)
                *p++ = digits[nibble];
        }
    }
    return p;
}
void log_line(std::string_view text) noexcept {
    const int fd = ::open(debug_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
        return;
    const long written = ::write(fd, text.data(), text.size());
    const long newline = ::write(fd, "\n", 1);
    const int closed = ::close(fd);
    (void)written;
    (void)newline;
    (void)closed;
}

bool valid_serial(std::string_view serial) noexcept {
    if (serial.empty() || serial.size() > max_serial)
        return false;
    for (const char c : serial) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '-';
        if (!ok)
            return false;
    }
    return true;
}
} // namespace

bool Fetcher::busy() const noexcept {
    return state != State::idle && state != State::done && state != State::failed;
}

void Fetcher::begin_failure(Failure reason) noexcept {
    if (failure == Failure::none) {
        failure = reason;
        char buffer[256]{};
        char *p = buffer;
        char *const end = buffer + sizeof(buffer);
        p = append(p, end, "FAIL f=");
        p = append_unsigned(p, end, static_cast<unsigned long>(reason));
        p = append(p, end, " err=");
        p = append_hex(p, end, static_cast<unsigned long>(static_cast<std::uint32_t>(error)));
        p = append(p, end, " http=");
        p = append_unsigned(p, end, static_cast<unsigned long>(http_status));
        p = append(p, end, " url=");
        p = append(p, end, std::string_view(url_.data()));
        log_line({buffer, static_cast<std::size_t>(p - buffer)});
    }
    state = State::finish;
}

void Fetcher::teardown() noexcept {
    if (request_ >= 0) {
        if (sceHttp2DeleteRequest(request_) != 0)
            error = -1;
        request_ = -1;
    }
    if (template_ >= 0) {
        if (sceHttp2DeleteTemplate(template_) != 0)
            error = -1;
        template_ = -1;
    }
    if (http_ >= 0) {
        if (sceHttp2Term(http_) != 0)
            error = -1;
        http_ = -1;
    }
    if (ssl_ >= 0) {
        if (sceSslTerm(ssl_) != 0)
            error = -1;
        ssl_ = -1;
    }
    if (pool_ >= 0) {
        if (sceNetPoolDestroy(pool_) != 0)
            error = -1;
        pool_ = -1;
    }
}

void Fetcher::cleanup() noexcept {
    if (descriptor_ >= 0) {
        (void)::close(descriptor_);
        descriptor_ = -1;
    }
    if (failure != Failure::none && path_[0] != 0)
        (void)::unlink(path_.data());
    teardown();
}

bool Fetcher::clear() noexcept {
    cleanup();
    state = State::idle;
    failure = Failure::none;
    operations = 0;
    received = 0;
    bytes_written = 0;
    http_status = 0;
    error = 0;
    chunk_length_ = 0;
    return true;
}

bool Fetcher::request(std::string_view serial) noexcept {
    if (busy())
        return false;
    clear();
    if (!valid_serial(serial)) {
        failure = Failure::invalid_serial;
        state = State::failed;
        return true;
    }
    std::size_t at = 0;
    for (const char c : std::string_view{url_prefix})
        url_[at++] = c;
    for (const char c : serial)
        url_[at++] = c;
    for (const char c : std::string_view{".jpg"})
        url_[at++] = c;
    url_[at] = 0;

    at = 0;
    for (const char c : std::string_view{cover_dir})
        path_[at++] = c;
    for (const char c : serial)
        path_[at++] = c;
    for (const char c : std::string_view{".jpg"})
        path_[at++] = c;
    path_[at] = 0;

    {
        char buffer[200]{};
        char *p = buffer;
        char *const end = buffer + sizeof(buffer);
        p = append(p, end, "REQ url=");
        p = append(p, end, std::string_view(url_.data()));
        log_line({buffer, static_cast<std::size_t>(p - buffer)});
    }
    state = net_ready ? State::pool_create : State::net_init;
    return true;
}

void Fetcher::step() noexcept {
    switch (state) {
    case State::net_init:
        ++operations;
        error = sceNetInit();
        if (error != 0) {
            begin_failure(Failure::net_init);
            return;
        }
        net_ready = true;
        state = State::pool_create;
        return;
    case State::pool_create:
        ++operations;
        pool_ = sceNetPoolCreate(agent, 32 * 1024, 0);
        if (pool_ < 0) {
            error = pool_;
            begin_failure(Failure::pool_create);
            return;
        }
        state = State::ssl_init;
        return;
    case State::ssl_init:
        ++operations;
        ssl_ = sceSslInit(256 * 1024);
        if (ssl_ < 0) {
            error = ssl_;
            begin_failure(Failure::ssl_init);
            return;
        }
        state = State::http_init;
        return;
    case State::http_init:
        ++operations;
        http_ = sceHttp2Init(pool_, ssl_, 256 * 1024, 1);
        if (http_ < 0) {
            error = http_;
            begin_failure(Failure::http_init);
            return;
        }
        state = State::template_create;
        return;
    case State::template_create:
        ++operations;
        template_ = sceHttp2CreateTemplate(http_, agent, 3, 1);
        if (template_ < 0) {
            error = template_;
            begin_failure(Failure::template_create);
            return;
        }
        state = State::request_create;
        return;
    case State::request_create:
        ++operations;
        request_ = sceHttp2CreateRequestWithURL(template_, "GET", url_.data(), 0);
        if (request_ < 0) {
            error = request_;
            begin_failure(Failure::request_create);
            return;
        }
        {
            // Bound every phase so an unreachable host cannot freeze the
            // render loop; results are logged for diagnosis.
            const int resolve = sceHttp2SetResolveTimeOut(request_, 10 * 1000 * 1000);
            const int connect = sceHttp2SetConnectTimeOut(request_, 10 * 1000 * 1000);
            const int send = sceHttp2SetSendTimeOut(request_, 10 * 1000 * 1000);
            const int recv = sceHttp2SetRecvTimeOut(request_, 10 * 1000 * 1000);
            const int total = sceHttp2SetTimeOut(request_, 20 * 1000 * 1000);
            const int redirect = sceHttp2SetAutoRedirect(request_, 1);
            char buffer[160]{};
            char *p = buffer;
            char *const end = buffer + sizeof(buffer);
            p = append(p, end, "CFG resolve=");
            p = append_hex(p, end, static_cast<unsigned long>(static_cast<std::uint32_t>(resolve)));
            p = append(p, end, " connect=");
            p = append_hex(p, end, static_cast<unsigned long>(static_cast<std::uint32_t>(connect)));
            p = append(p, end, " send=");
            p = append_hex(p, end, static_cast<unsigned long>(static_cast<std::uint32_t>(send)));
            p = append(p, end, " recv=");
            p = append_hex(p, end, static_cast<unsigned long>(static_cast<std::uint32_t>(recv)));
            p = append(p, end, " total=");
            p = append_hex(p, end, static_cast<unsigned long>(static_cast<std::uint32_t>(total)));
            p = append(p, end, " redirect=");
            p = append_hex(p, end, static_cast<unsigned long>(static_cast<std::uint32_t>(redirect)));
            log_line({buffer, static_cast<std::size_t>(p - buffer)});
        }
        state = State::send;
        return;
    case State::send:
        ++operations;
        error = sceHttp2SendRequest(request_, nullptr, 0);
        if (error != 0) {
            begin_failure(Failure::send);
            return;
        }
        state = State::status;
        return;
    case State::status:
        ++operations;
        error = sceHttp2GetStatusCode(request_, &http_status);
        if (error != 0) {
            begin_failure(Failure::status);
            return;
        }
        if (http_status != 200) {
            begin_failure(Failure::status);
            return;
        }
        state = State::file_create;
        return;
    case State::file_create:
        ++operations;
        descriptor_ = ::open(path_.data(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (descriptor_ < 0) {
            begin_failure(Failure::file_create);
            return;
        }
        state = State::read;
        return;
    case State::read: {
        ++operations;
        const int got = sceHttp2ReadData(request_, chunk_.data(), chunk_.size());
        if (got < 0) {
            error = got;
            begin_failure(Failure::read);
            return;
        }
        if (got == 0) {
            state = State::finish;
            return;
        }
        chunk_length_ = static_cast<std::size_t>(got);
        received += chunk_length_;
        if (received > max_cover_bytes) {
            begin_failure(Failure::too_large);
            return;
        }
        state = State::write;
        return;
    }
    case State::write: {
        ++operations;
        const long put = ::write(descriptor_, chunk_.data(), chunk_length_);
        if (put < 0 || static_cast<std::size_t>(put) != chunk_length_) {
            begin_failure(Failure::write);
            return;
        }
        bytes_written += static_cast<std::size_t>(put);
        state = State::read;
        return;
    }
    case State::finish:
        ++operations;
        if (failure == Failure::none && received == 0)
            failure = Failure::empty;
        cleanup();
        if (failure == Failure::none) {
            char buffer[128]{};
            char *p = buffer;
            char *const end = buffer + sizeof(buffer);
            p = append(p, end, "OK bytes=");
            p = append_unsigned(p, end, static_cast<unsigned long>(received));
            p = append(p, end, " http=");
            p = append_unsigned(p, end, static_cast<unsigned long>(http_status));
            p = append(p, end, " url=");
            p = append(p, end, std::string_view(url_.data()));
            log_line({buffer, static_cast<std::size_t>(p - buffer)});
        }
        state = failure == Failure::none ? State::done : State::failed;
        return;
    case State::idle:
    case State::done:
    case State::failed:
        return;
    }
}

std::string_view Fetcher::summary() const noexcept {
    switch (state) {
    case State::idle:
        return "DOWNLOAD IDLE";
    case State::done:
        return "COVER DOWNLOADED";
    case State::failed:
        switch (failure) {
        case Failure::invalid_serial:
            return "DOWNLOAD: BAD GAME ID";
        case Failure::net_init:
            return "DOWNLOAD: NET INIT FAILED";
        case Failure::pool_create:
            return "DOWNLOAD: NET POOL FAILED";
        case Failure::ssl_init:
            return "DOWNLOAD: SSL INIT FAILED";
        case Failure::http_init:
            return "DOWNLOAD: HTTP INIT FAILED";
        case Failure::template_create:
            return "DOWNLOAD: TEMPLATE FAILED";
        case Failure::request_create:
            return "DOWNLOAD: REQUEST FAILED";
        case Failure::send:
            return "DOWNLOAD: SEND FAILED";
        case Failure::status:
            return "DOWNLOAD: HTTP STATUS REJECTED";
        case Failure::file_create:
            return "DOWNLOAD: FILE CREATE FAILED";
        case Failure::read:
            return "DOWNLOAD: RECEIVE FAILED";
        case Failure::write:
            return "DOWNLOAD: FILE WRITE FAILED";
        case Failure::too_large:
            return "DOWNLOAD: COVER TOO LARGE";
        case Failure::empty:
            return "DOWNLOAD: EMPTY RESPONSE";
        case Failure::cleanup:
        case Failure::none:
            break;
        }
        return "DOWNLOAD FAILED";
    default:
        return "DOWNLOADING COVER";
    }
}
} // namespace cover_fetch
