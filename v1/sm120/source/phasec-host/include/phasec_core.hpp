#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace phasec {

struct Epochs {
    std::uint64_t session = 0;
    std::uint64_t job = 0;
    std::uint64_t extranonce = 0;
    std::uint64_t target = 0;
    friend bool operator==(const Epochs&, const Epochs&) = default;
};

struct Endpoint {
    std::string host;
    std::uint16_t port = 0;
};

struct JobTemplate {
    std::string job_id;
    std::vector<std::uint8_t> prevhash;
    std::vector<std::uint8_t> coinb1;
    std::vector<std::uint8_t> coinb2;
    std::vector<std::array<std::uint8_t, 32>> merkle;
    std::array<std::uint8_t, 4> version{};
    std::array<std::uint8_t, 4> nbits{};
    std::array<std::uint8_t, 4> ntime{};
    bool clean = false;
    double diff = 1.0;
};

struct ConcreteWork {
    std::array<std::uint32_t, 20> words{};
    std::array<std::uint32_t, 8> target{};
    std::string job_id;
    std::vector<std::uint8_t> xnonce2;
    Epochs epochs{};
    double pool_diff = 1.0;
    std::uint64_t serial = 0;
};

struct Candidate {
    ConcreteWork work;
    std::uint32_t nonce = 0;
    std::array<std::uint32_t, 8> hash{};
};

struct Counters {
    std::atomic<std::uint64_t> accepted{0};
    std::atomic<std::uint64_t> rejected{0};
    std::atomic<std::uint64_t> stale_pool{0};
    std::atomic<std::uint64_t> stale_local{0};
    std::atomic<std::uint64_t> send_fail{0};
};

Endpoint parse_endpoint(std::string_view url);
std::vector<std::uint8_t> hex_decode(std::string_view hex);
std::string hex_encode(const std::uint8_t* p, std::size_t n);
std::string hex_encode(const std::vector<std::uint8_t>& v);
std::string nonce_hex(std::uint32_t nonce);
std::array<std::uint32_t, 8> diff_to_target(double diff);
std::array<std::uint32_t, 8> lyra2v2_pool_target(double pool_diff);
void increment_le(std::vector<std::uint8_t>& value, bool& wrapped);

class SharedState {
public:
    void stop();
    bool stopped() const;

    void begin_session();
    void end_session();
    void set_authorized(bool value);
    void set_subscription(const std::vector<std::uint8_t>& xnonce1, std::size_t xnonce2_size);
    void set_extranonce(const std::vector<std::uint8_t>& xnonce1, std::size_t xnonce2_size);
    void set_next_difficulty(double diff);
    void set_job(JobTemplate job);

    std::optional<ConcreteWork> make_work();
    bool still_current(const ConcreteWork& work) const;
    bool run_if_current(const ConcreteWork& work, const std::function<bool()>& action);
    bool run_if_submit_valid(const ConcreteWork& work, const std::function<bool()>& action);
    std::uint64_t change_serial() const;
    bool wait_for_change(std::uint64_t previous, std::uint32_t timeout_ms) const;

    Epochs epochs() const;
    std::string current_job_id() const;

private:
    mutable std::mutex mu_;
    mutable std::condition_variable cv_;
    bool stopped_ = false;
    bool connected_ = false;
    bool authorized_ = false;
    bool have_subscription_ = false;
    bool have_job_ = false;
    bool xnonce2_exhausted_ = false;
    Epochs epochs_{};
    std::uint64_t change_serial_ = 0;
    std::uint64_t work_serial_ = 0;
    double next_diff_ = 1.0;
    std::vector<std::uint8_t> xnonce1_;
    std::vector<std::uint8_t> xnonce2_;
    JobTemplate job_{};
    struct ValidJob {
        std::string job_id;
        std::array<std::uint32_t, 8> target{};
        std::uint32_t ntime = 0;
        std::uint64_t session = 0;
        std::uint64_t extranonce = 0;
    };
    std::vector<ValidJob> submit_valid_jobs_;

    void changed_unlocked();
};

class CandidateQueue {
public:
    explicit CandidateQueue(std::size_t capacity);
    bool push(Candidate candidate, const std::atomic<bool>& stop_flag);
    std::optional<Candidate> try_pop();
    void close();
    std::size_t size() const;

private:
    std::size_t capacity_;
    mutable std::mutex mu_;
    std::condition_variable not_full_;
    std::deque<Candidate> queue_;
    bool closed_ = false;
};

} // namespace phasec
