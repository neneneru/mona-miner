#include "console_ui.hpp"

#include <array>
#include <cmath>
#include <cstdio>

int main() {
    phasec::Candidate candidate;
    candidate.work.pool_diff = 0.25;
    candidate.work.target = phasec::lyra2v2_pool_target(candidate.work.pool_diff);
    candidate.hash = candidate.work.target;

    const double expected = candidate.work.pool_diff / 256.0;
    const double actual = phasec::ui::share_difficulty(candidate);
    if (std::abs(actual - expected) > 1e-12) {
        std::fprintf(stderr, "share difficulty mismatch: got %.15g expected %.15g\n", actual, expected);
        return 1;
    }

    phasec::ui::set_hashrate_mhs(478.625);
    if (std::abs(phasec::ui::current_hashrate_mhs() - 478.625) > 1e-12) return 2;

    phasec::ui::configure_output(false, 60);
    if (phasec::ui::all_shares_enabled()) return 9;
    if (phasec::ui::summary_interval_seconds() != 60) return 10;
    phasec::ui::configure_output(false, 10);
    if (phasec::ui::summary_interval_seconds() != 10) return 11;
    phasec::ui::configure_output(true, 30);
    if (!phasec::ui::all_shares_enabled()) return 12;
    if (phasec::ui::summary_interval_seconds() != 30) return 13;
    // Restore the default presentation contract for the remaining tests.
    phasec::ui::configure_output(false, 60);

    phasec::ui::set_pool_difficulty(0.25);
    phasec::ui::set_share_totals(841, 0);
    phasec::ui::reset_summary_baseline(0);
    const auto first = phasec::ui::take_summary_snapshot(386.4);
    if (std::abs(first.mhs - 386.4) > 1e-12) return 3;
    if (first.accepted != 841 || first.total != 841 || first.accepted_delta != 841) return 4;
    if (std::abs(first.pool_diff - 0.25) > 1e-12) return 5;
    if (phasec::ui::summary_body(first) !=
        "386.40 MH/s | accepted: 841/841 (+841) | diff 0.25") return 8;

    phasec::ui::set_share_totals(1675, 0);
    const auto second = phasec::ui::take_summary_snapshot(388.1);
    if (second.accepted != 1675 || second.total != 1675 || second.accepted_delta != 834) return 6;

    phasec::ui::set_share_totals(1675, 1);
    const auto third = phasec::ui::take_summary_snapshot(387.9);
    if (third.accepted != 1675 || third.rejected != 1 || third.total != 1676 || third.accepted_delta != 0) return 7;

    std::puts("console_ui_tests: PASS");
    return 0;
}
