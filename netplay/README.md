# Netplay

Rollback netplay for tgg-melee: two games play matches against each other over UDP, on
Linux, macOS and Windows, and a game on one plays a game on another.

## For players

Netplay has no screen of its own. Install a connect mod with it, like `tgg.direct-connect`
(host, or type the host's address), and that mod opens the session and starts the matches.

Each game runs ahead on a guess of the other player's input. When the real input arrives and
differs, the game rolls back to that frame and runs the frames since again. Matches played
offline are untouched: rollback is on only while a session is open.

Settings, on the Mods page. They're your own and don't have to match the other player's.

| Setting | Default | Does |
|---|---|---|
| Input delay (frames) | 2 | frames between pressing a button and it happening, 0 to 15. More delay means fewer rollbacks |
| Your controller | Any | the controller you play with: Any (the first one plugged in) or Port 1 to 4. It plays your side's port wherever it's plugged in |
| When hosting, mods that must match | Gameplay mods | Gameplay mods: display mods (skins, HUDs, the replay recorder) can differ. All mods: every mod has to match, display mods too, for tournaments and the like. The host's choice counts |

Both games need the same game and the same gameplay mods. They check when they meet, and the
session ends if they differ. On one platform that's the same build and mod-set id. Between
platforms (Linux, macOS and Windows) it's the same game version and the same mod versions,
built from the same reviewed source. Display mods, the ones textures.gg's review marked as
only changing what you see, can differ unless the host wants every mod to match. If a match
stops agreeing, the log names the display mods only one of you has: the likely cause.

## For mod makers

### The API

A connect mod (a direct-connect screen, a lobby, a matchmaker) opens a session, talks to the
other game with messages, and starts a match from its screen. Netplay does the rest: inputs,
guessing, rollback, pacing and desync checks. List it in your manifest and include
`include/netplay.h` (copy it, or add netplay's `include/` to the include path):

```json
"depends": { "tgg.netplay": "^0.4" }
```

| Function | Does |
|---|---|
| `netplay_open(side, peer, port, &why)` | opens a session. Side 0 with `peer` NULL waits on `port` for anyone; otherwise it says hello to `peer` ("host:port") |
| `netplay_status(&info)` | the state (waiting, connecting, connected, starting, playing, failed), the peer, the round trip, why it failed |
| `netplay_send(data, len)`, `netplay_receive(buf, cap)` | messages between the two games, up to 400 bytes, whole, once and in order |
| `netplay_play(data)` | from a screen: side 0 passes the match (`StartMeleeData`), side 1 passes NULL to take it. Both leave for the match together and come back after |
| `netplay_close()` | ends the session and tells the other game |

Call these from presentation code (a screen's functions or a PRESENT event), never from
simulation code. `tgg.direct-connect` is a complete example of a mod built on them.

### How it works

```text
src/session.c     the session: hello, keeping in touch, messages, agreeing on a match; the API
src/match.c       a match: inputs, rollback, confirming frames, checksums, pacing
src/wire.c        the packets, byte by byte
src/net.c         the UDP socket, over the port's tgg/net.h
src/fakenet.c     a fake network for testing: latency, jitter, loss, reordering
src/replaytest.c  the replay test
src/slp.c         reading a Slippi replay
```

- **Hello.** Each side says hello every 100 ms until the other has its hello: the game build,
  the platform, the mod-set ids, the side. Side 0 with no peer address plays the first game
  whose hello arrives.
- **Between matches.** A lobby packet every 100 ms carries the round trip, unacknowledged
  messages, and which of the last match's inputs this side has. Ten seconds of silence ends
  the session.
- **A match.** Side 0's `netplay_play` sends the match number, a new seed and the setup as
  the console lays it out (`tgg_match_to_console`); side 1's says it's ready. Each starts once
  it has both, and holds the first frame until the other's first packet arrives. Every packet
  carries a hash of the setup, and a mismatch ends the session.
- **Inputs** (`pad_read` event). Your controller is read when a frame first runs and plays the
  input delay later, in your side's port. An input is the controller's report and the state
  the game made of it, so the other game sets both exactly. Every packet carries every input
  the other side hasn't acknowledged (up to 40), so a lost packet costs only time.
- **Rollback** (`advance` event). Each frame gets the other side's input if it has arrived,
  else the last one that did. When the real input differs from the guess, `tgg_rollback`
  goes back to that frame.
- **Confirming.** A frame is confirmed once both inputs are known and it has run on them
  (`tgg_rollback_confirm`). Packets carry a confirmed frame's `tgg_frame_checksum`, and the
  first frame that differs is logged as a desync.
- **Pacing.** A frame is held when running it would guess further ahead than a rollback
  reaches, and once in ten frames when this game is more than a frame ahead of the other.
- **The end** (`match_end`, `quit` events). The port holds the end of the match until its
  last frame is confirmed, and inputs keep going until the other side has them all. The log
  has each match's result, last checksum, rollbacks, holds and round trip.

### Without a connect mod

Environment variables open a session for a match the boot sets up (`TGG_BOOT_MATCH`):

```sh
# one machine, side 0, port 1
TGG_NETPLAY_PEER=192.168.1.20:51000 TGG_NETPLAY_SIDE=0 TGG_BOOT_MATCH=fox,marth,31 tgg mod dev
# the other machine, side 1, port 2
TGG_NETPLAY_PEER=192.168.1.10:51000 TGG_NETPLAY_SIDE=1 TGG_BOOT_MATCH=fox,marth,31 tgg mod dev
```

| Variable | Meaning |
|---|---|
| `TGG_NETPLAY_PEER` | the other game, `host:port` |
| `TGG_NETPLAY_PORT` | the UDP port this game listens on, default 51000 |
| `TGG_NETPLAY_SIDE` | 0 or 1, one of each: side 0 picks the match's seed |
| `TGG_NETPLAY_PLAYERS` | the ports side 0 and side 1 play, default `1,2` |
| `TGG_NETPLAY_DELAY` | frames of input delay, over the setting |
| `TGG_NETPLAY_ROLLBACK` | the most frames to roll back, default 8 (up to 30) |
| `TGG_NETPLAY_FAKE` | a fake network: `latency=40,jitter=10,loss=5` (one way, ms and percent) |
| `TGG_NETPLAY_REPLAY` | the replay test, below |

### The replay test

`TGG_NETPLAY_REPLAY=<file.slp>` has two games play a Slippi replay's match over the network.
Each plays one player's recorded inputs (`TGG_NETPLAY_SIDE` picks which) and sets each
frame's RNG seed to the replay's, as Slippi's netplay keeps the two consoles' seeds together.
With the port's replay check on (`TGG_REPLAY` and `TGG_REPLAY_CHECK_ONLY=1`), both games must
end where the console did, and each prints `replay: MATCH` and a summary of rollbacks, holds,
round trip and matched checksums:

```sh
for side in 0 1; do
  TGG_REPLAY=game.slp TGG_REPLAY_CHECK_ONLY=1 TGG_SYNC_DISC=0 TGG_NETPLAY_REPLAY=game.slp \
  TGG_NETPLAY_SIDE=$side TGG_NETPLAY_PORT=$((52000 + side)) TGG_NETPLAY_PEER=127.0.0.1:$((52001 - side)) \
  TGG_NETPLAY_DELAY=2 TGG_NETPLAY_FAKE=latency=40,jitter=10,loss=5 \
  "$(tgg port path)/tgg-melee" melee.iso &
done; wait
```

Only replays of online matches work: online matches never pause
(slippi-ssbm-asm `Online/Core/InitPause.asm`), so every replay frame is a match frame. The
replay's Slippi rule mods have to be installed.

## Requirements

tgg-melee 0.1.0-rc.9 or later: netplay talks to the network through the port's `tgg/net.h`,
which older builds don't have.

## Build

```sh
tgg mod build    # packs build/tgg.netplay-<version>.zip
tgg mod dev      # build, install and run the game
```
