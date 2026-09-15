// On-demand PS2 cover art downloader. SPDX-License-Identifier: GPL-3.0-or-later
//
// Fetches https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/
// default/<SERIAL>.jpg into /data/PS2/covers/<SERIAL>.jpg using the SDK HTTP/2
// client. One network/disk operation runs per `step()` so the render loop can
// keep polling input while a download is in flight.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cover_fetch {
inline constexpr char url_prefix[] =
    "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default/";
inline constexpr char cover_dir[] = "/data/PS2/covers/";
inline constexpr std::size_t max_serial = 24;
inline constexpr std::size_t max_url = 160;
inline constexpr std::size_t max_path = 96;
inline constexpr std::size_t chunk_bytes = 32U << 10;
inline constexpr std::size_t max_cover_bytes = 1U << 20; // 1 MiB cap while streaming

enum class State {
    idle,
    net_init,
    pool_create,
    ssl_init,
    http_init,
    template_create,
    request_create,
    send,
    status,
    file_create,
    read,
    write,
    finish,
    done,
    failed,
};

enum class Failure {
    none,
    invalid_serial,
    net_init,
    pool_create,
    ssl_init,
    http_init,
    template_create,
    request_create,
    send,
    status,
    file_create,
    read,
    write,
    too_large,
    empty,
    cleanup,
};

class Fetcher final {
  public:
    State state = State::idle;
    Failure failure = Failure::none;
    std::size_t operations = 0;
    std::size_t received = 0;
    std::size_t bytes_written = 0;
    int http_status = 0;
    int error = 0;

    bool busy() const noexcept;
    bool clear() noexcept;
    bool request(std::string_view serial) noexcept;
    void step() noexcept;
    std::string_view summary() const noexcept;

  private:
    std::array<char, max_url> url_{};
    std::array<char, max_path> path_{};
    std::array<std::uint8_t, chunk_bytes> chunk_{};
    std::size_t chunk_length_ = 0;
    int pool_ = -1;
    int ssl_ = -1;
    int http_ = -1;
    int template_ = -1;
    int request_ = -1;
    int descriptor_ = -1;

    void begin_failure(Failure reason) noexcept;
    void cleanup() noexcept;
    void teardown() noexcept;
};
} // namespace cover_fetch
