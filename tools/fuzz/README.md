# The fuzz drivers

What other people send a game reaches three mods' parsers: netplay's packets
(another player, a STUN or TURN server), replay-player's `.slp` reader (a
replay anyone can share), and tgg.http's reading of a server's answers. Each
has a libFuzzer driver here, built with ASan and UBSan against the mod's own
sources. A crash, a hang or a runaway allocation is a finding to fix.

```sh
nix develop .#fuzz --command tools/fuzz/run.sh <tgg-melee>/build/tgg-melee-sdk /tmp/fuzz 600 4
```

That builds every driver into `tools/fuzz/build/` and runs each for 600
seconds on 4 processes. Corpora grow in `/tmp/fuzz/<driver>/corpus`, and
findings land beside them as `crash-*`, `timeout-*` and `oom-*`. Name drivers
after the job count to run only some. `SLP_SEEDS=<folder of .slp files>` seeds
the replay driver with real replays.

To see one finding with its stack, run the driver on it in the same shell:

```sh
nix develop .#fuzz --command tools/fuzz/build/replay-player/fuzz /tmp/fuzz/replay-player/crash-…
```

## How they reach the code

`port.c` stands in for tgg-melee. It's compiled against the SDK's headers,
so each function has the type a mod expects. The network is a queue the driver
fills, the clock moves only when the driver says, and `tgg_net_random` gives
zeros, so the fuzzer can answer a STUN or TURN transaction. Events a mod
registers (`TGG_ON`) are found in its records the way the port finds them.

- **netplay** opens a session as host or joiner, directly or through the
  meeting point. It then plays a script of raw datagrams from peers and
  servers, and packets `wire_encode` builds from fuzz bytes with the session's
  token and the same build and mods, so the fuzzer gets past the hello into
  lobbies, profiles and matches. It also runs frames, match frames with
  rollback, and API calls. `unlocks.c` and `replaytest.c` are left out: they
  read the save and files, not the network.
- **replay-player** writes each input to a file and reads it as playing
  (`slp_read`, then the rules from its gecko list) and as listing
  (`replay_read_summary`) do.
- **http** replaces `conn.c` (TCP and TLS) with a connection that returns
  fuzz bytes in fuzz-sized reads. It starts a request or a WebSocket, with or
  without a good upgrade, and feeds it heads, sized and chunked bodies, frames
  and early closes.

Signed overflow wraps in the real build (`-fwrapv`), so the drivers build
with it too and UBSan doesn't report it.
