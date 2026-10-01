# Mona Miner v2 SM120 — private product prototype

Developer Fee is fixed at **2.00%** of completed, verified, non-duplicated local
mining work under the USER-first accounting contract. There is no fee-rate switch.
A complete healthy frame assigns USER 98q and DEVFEE 2q; q is 64 native batches.
Incomplete runs can finish below 2%. Outages/restarts do not create catch-up debt.
USER unavailable stops new work; developer unavailable leaves USER mining active.

One explicit GPU per process, two persistent Stratum sessions. Pool targets come
from the server difficulty and subsequent job; no client-side forced difficulty.
Share limits count USER accepted with in-flight/UNKNOWN reservations.

Runtime credentials enter through standard input only. `examples/Start-Mining.ps1`
asks for endpoint, worker and mining password without saving them to a file, argv,
or environment. This is not a guarantee of secure erasure of all managed memory.
Do not supply Web/withdrawal/wallet-control secrets.

Help and invalid CLI do not create network/GPU resources. Benchmark creates no
pool connection. Fixed GPU performance sources, launch shapes, and sanitized images
are not retuned by this host. No telemetry, updater or hidden pool redirect.

This is a review prototype, not a public release. Windows Build, Mock, GPU Local,
and separately reviewed live validation are required before release decisions.
