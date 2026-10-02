#include "phasec_core.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <thread>

using namespace phasec;

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

template<class F> void throws(F&& f) {
    bool ok = false;
    try { f(); } catch (...) { ok = true; }
    CHECK(ok);
}

JobTemplate job(std::string id = "job-A") {
    JobTemplate j;
    j.job_id = std::move(id);
    j.prevhash.assign(32, 0x11);
    j.coinb1 = hex_decode("01020304");
    j.coinb2 = hex_decode("05060708");
    j.version = {0x20,0x00,0x00,0x00};
    j.nbits = {0xff,0xff,0x00,0x1d};
    j.ntime = {0x12,0x34,0x56,0x78};
    return j;
}

}

int main() {
    {
        auto e = parse_endpoint("stratum+tcp://pool.example:8888");
        CHECK(e.host == "pool.example"); CHECK(e.port == 8888);
        auto v6 = parse_endpoint("stratum+tcp://[::1]:3333");
        CHECK(v6.host == "::1" && v6.port == 3333);
        throws([] { (void)parse_endpoint("https://example.com:443"); });
        throws([] { (void)parse_endpoint("stratum+tcp://example.com"); });
        throws([] { (void)parse_endpoint("stratum+tcp://example.com:0"); });
        throws([] { (void)parse_endpoint("stratum+tcp://example.com:65536"); });
    }
    CHECK(nonce_hex(0x12345678u) == "78563412");
    {
        auto v = hex_decode("00a1FF");
        CHECK(v.size() == 3 && v[0] == 0 && v[1] == 0xa1 && v[2] == 0xff);
        CHECK(hex_encode(v) == "00a1ff");
        throws([] { (void)hex_decode("abc"); });
        throws([] { (void)hex_decode("zz"); });
    }
    {
        std::vector<std::uint8_t> x{0xff,0x00}; bool wrapped=false;
        increment_le(x, wrapped);
        CHECK(!wrapped && x[0] == 0 && x[1] == 1);
        x = {0xff,0xff}; increment_le(x, wrapped);
        CHECK(wrapped && x[0] == 0 && x[1] == 0);
    }
    {
        // Pinned ccminer ALGO_LYRA2v2 maps pool difficulty D to generic target(D/256).
        CHECK(lyra2v2_pool_target(256.0) == diff_to_target(1.0));
        CHECK(lyra2v2_pool_target(512.0) == diff_to_target(2.0));
        throws([] { (void)lyra2v2_pool_target(0.0); });
        throws([] { (void)diff_to_target(INFINITY); });
    }
    {
        SharedState s;
        s.begin_session();
        s.set_subscription(hex_decode("a1b2c3d4"), 4);
        s.set_authorized(true);
        s.set_next_difficulty(256.0);
        s.set_job(job());
        auto a = s.make_work(); auto b = s.make_work();
        CHECK(a.has_value() && b.has_value());
        CHECK(hex_encode(a->xnonce2) == "00000000");
        CHECK(hex_encode(b->xnonce2) == "01000000");
        CHECK(s.still_current(*a));
        auto old_target = a->target;
        s.set_next_difficulty(512.0); // applies to next notify, not current work
        CHECK(s.still_current(*a));
        s.set_job(job("job-B"));
        CHECK(!s.still_current(*a));
        auto c = s.make_work(); CHECK(c.has_value());
        CHECK(c->target == diff_to_target(2.0));
        CHECK(c->target != old_target);
        s.set_extranonce(hex_decode("0102"), 4);
        CHECK(!s.still_current(*c));
        s.set_job(job("job-C"));
        auto d=s.make_work(); CHECK(d.has_value());
        s.end_session(); CHECK(!s.still_current(*d));
    }
    {
        // A fresh Stratum subscription resets next difficulty to 1.0 like pinned ccminer.
        SharedState s;
        s.begin_session(); s.set_subscription(hex_decode("01"), 4); s.set_authorized(true);
        s.set_next_difficulty(512.0);
        s.end_session();
        s.begin_session(); s.set_subscription(hex_decode("02"), 4); s.set_authorized(true);
        s.set_job(job("reconnect"));
        auto w=s.make_work(); CHECK(w.has_value());
        CHECK(w->target == lyra2v2_pool_target(1.0));
    }
    {
        // Final validation and externally-visible action are one state-locked operation.
        SharedState s;
        s.begin_session(); s.set_subscription(hex_decode("01"), 4); s.set_authorized(true);
        s.set_next_difficulty(256.0); s.set_job(job()); auto w=s.make_work(); CHECK(w.has_value());
        bool called=false;
        CHECK(s.run_if_current(*w, [&]{ called=true; return true; })); CHECK(called);
        s.set_job(job("new")); called=false;
        CHECK(!s.run_if_current(*w, [&]{ called=true; return true; })); CHECK(!called);
    }
    {
        // clean=false switches scanning to the new job but keeps the prior job submit-valid
        // within the same session/extranonce. clean=true and set_extranonce invalidate it.
        SharedState s;
        s.begin_session(); s.set_subscription(hex_decode("01"), 4); s.set_authorized(true);
        s.set_next_difficulty(256.0);
        auto old_job = job("old"); old_job.clean = true;
        s.set_job(old_job);
        auto old_a=s.make_work(); auto old_b=s.make_work();
        CHECK(old_a.has_value() && old_b.has_value());

        auto next_job = job("next"); next_job.clean = false;
        s.set_job(next_job);
        CHECK(!s.still_current(*old_a));
        bool called=false;
        CHECK(s.run_if_submit_valid(*old_a, [&]{ called=true; return true; })); CHECK(called);
        called=false;
        CHECK(s.run_if_submit_valid(*old_b, [&]{ called=true; return true; })); CHECK(called);
        auto next_work=s.make_work(); CHECK(next_work.has_value());
        called=false; CHECK(s.run_if_submit_valid(*next_work, [&]{ called=true; return true; })); CHECK(called);

        auto clean_job = job("clean"); clean_job.clean = true;
        s.set_job(clean_job);
        called=false; CHECK(!s.run_if_submit_valid(*old_a, [&]{ called=true; return true; })); CHECK(!called);
        called=false; CHECK(!s.run_if_submit_valid(*next_work, [&]{ called=true; return true; })); CHECK(!called);
        auto clean_work=s.make_work(); CHECK(clean_work.has_value());
        called=false; CHECK(s.run_if_submit_valid(*clean_work, [&]{ called=true; return true; })); CHECK(called);

        s.set_extranonce(hex_decode("0203"), 4);
        called=false; CHECK(!s.run_if_submit_valid(*clean_work, [&]{ called=true; return true; })); CHECK(!called);
    }
    {
        // Bounded queue uses backpressure rather than silently dropping a third candidate.
        CandidateQueue q(2); std::atomic<bool> stop{false};
        Candidate c1{}, c2{}, c3{}; c1.nonce=1; c2.nonce=2; c3.nonce=3;
        CHECK(q.push(c1,stop)); CHECK(q.push(c2,stop));
        std::atomic<bool> third_done{false};
        std::thread t([&]{ CHECK(q.push(c3,stop)); third_done.store(true); });
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        CHECK(!third_done.load());
        auto first=q.try_pop(); CHECK(first && first->nonce==1);
        for (int i=0;i<100 && !third_done.load();++i) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        CHECK(third_done.load()); t.join();
        auto second=q.try_pop(); auto third=q.try_pop();
        CHECK(second && second->nonce==2); CHECK(third && third->nonce==3);
        q.close();
    }
    {
        // The nonce scan cursor is deliberately 64-bit; 0xffffffff + 1 is 2^32, not 0.
        const std::uint64_t last = 0xffffffffull;
        CHECK(last + 1ull == (1ull << 32));
    }

    if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    std::puts("phasec_core_tests: PASS");
    return 0;
}
