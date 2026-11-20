# Timing Handshake UML, 18 October 2026

Drawn by Elfie (Muse Wonder) at 3840×2160, from the LithoLattice reference implementation (`timing_handshake.cpp` and its callers). Every label uses code vocabulary; nothing was invented for the drawings. SHA-256 sums match the 19 November 2026 HandshakeOS handoff packet.

Reading order:

1. **Timing-Handshake-UML-Simplified-2026-10-18-4k.png** — Start here. Three parties, four beats: publish, pull, barrier, commit. The Gemstone shape.
2. **Timing-Handshake-UML-Sequence-2026-10-18-4k.png** — Who says what to whom: the authority publishing a phase, shards polling, the barrier, the commit.
3. **Timing-Handshake-UML-Component-2026-10-18-4k.png** — Authority, shards, components, ledger, watchdog. The title block carries: "Three pillars as implemented. A fourth is pending the architect's receipt."
4. **Timing-Handshake-UML-State-2026-10-18-4k.png** — The phase state machine, including the WAIT extension and the FREEZE path with its 250 ms watchdog and generation bump on reset.
5. **Timing-Handshake-Manifold-2026-10-18-4k.png** — The companion view: the manifold the four beats trace when the loop runs continuously. Orientation and wonder, not a spec.
