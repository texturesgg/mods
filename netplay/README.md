# Netplay

Rollback netplay for tgg-melee: two games play matches over UDP. Each game runs ahead on a
guess of the other player's input, and when the real input arrives and differs, it rolls back
to that frame and runs the frames since again (the port's [rollback API](https://textures.gg/docs/mods/latest/rollback)).

Netplay is the session and the matches; it has no screen of its own. A connect mod opens a
session and starts matches through its API: [`tgg.direct-connect`](../direct-connect) is the
one to play with (host, or type the host's address). Matches played offline are untouched:
rollback is on only while a session is open.

## The API

`include/netplay.h` (`"depends": { "tgg.netplay": "^0.2" }`):

| Function | Does |
|---|---|
| `netplay_open(side, peer, port, &why)` | opens a session: side 0 waits on `port` for anyone (`peer` NULL), or both name a `peer` ("host:port") |
| `netplay_status(&info)` | the state (waiting, connecting, connected, starting, playing, failed), the peer, the round trip, why it failed |
| `netplay_send(data, len)`, `netplay_receive(buf, cap)` | messages between the two games, up to 400 bytes, whole, once and in order |
| `netplay_play(data)` | from a screen: side 0 passes the match (`StartMeleeData`), side 1 passes NULL to take it; both leave for it together and come back after |
| `netplay_close()` | ends the session and tells the other game |

## Settings

On the Mods page, the player's own (they don't have to match):

| Setting | Meaning |
|---|---|
| Input delay (frames) | default 2 |
| Your controller | Any (the first one plugged in), or a port. It plays your side's port wherever it's plugged in |

## Without a connect mod

The environment can open a session for a match the boot sets up, as the tests do:

```sh
# this machine, side 0, port 1
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
| `TGG_NETPLAY_FAKE` | a fake network for tests: `latency=40,jitter=10,loss=5` (one way, ms and percent) |
| `TGG_NETPLAY_REPLAY` | the replay test: play a Slippi replay's match, below |

Both games must run the same game and the same mods; the log says so when they meet, and the
session ends when they don't. On one platform that's the same build (`tgg_game_build`) and
mod-set id; a Linux game and a macOS game can't have the same libraries, so between platforms
it's the same game version and `tgg_mod_set_versions`, and the log says so.

## How it works

```text
src/session.c     the session: hello, keeping in touch, messages, agreeing on a match; the API
src/match.c       a match: inputs, rollback, confirming frames, checksums, pacing
src/wire.c        what goes over the network: inputs and packets, byte by byte
src/net.c         the UDP socket (POSIX); the one file Winsock would replace
src/fakenet.c     the fake network: latency, jitter, loss, reordering
src/replaytest.c  the replay test
src/slp.c         reading a Slippi replay (the replay player's reader)
```

- **Hello.** Each side says hello every 100 ms until the other has its hello: the game build,
  the platform, the mod-set ids, the side. Side 0 with no peer address plays the first game
  whose hello arrives.
- **Between matches.** A lobby packet every 100 ms: the round trip, the messages the other
  side hasn't acknowledged, and which of the last match's inputs this side has. Nothing heard
  for 10 seconds ends the session.
- **A match.** Side 0's `netplay_play` sends the match (its number, a new seed, and the setup
  as the console lays it out, `tgg_match_to_console`); side 1's says it's ready. Each starts
  once it has both, and holds the match's first frame until the other's first packet of the
  match arrives. Every packet carries a hash of the match's setup, and a different one ends
  the session.
- **Inputs.** Your controller is read in `pad_read` when a frame first runs and plays
  the input delay later, in your side's port. An input is the controller's report and the
  state the game made of it, so the other game sets both exactly as yours did. Ports nobody
  plays read as empty on both games. Every packet carries every input the other side hasn't
  acknowledged (up to 40), so a lost packet costs only time.
- **Rollback.** `pad_read` gives each frame both ports' inputs: yours, and the other side's if
  it has arrived, else the last one that did. The guess is kept, and when the real input
  arrives and differs, `advance` calls `tgg_rollback` to that frame.
- **Confirming.** A frame is confirmed once both inputs are known and it has run on them
  (`tgg_rollback_confirm`). Each packet carries the checksum of a confirmed frame, and each
  side compares it with its own (`tgg_frame_checksum`): the first frame that differs is logged
  as a desync.
- **Pacing.** A frame is held when running it would guess further ahead than a rollback
  reaches, and once in ten frames when this game is more than a frame ahead of the other
  (from the other side's frame, how long ago it was heard and half the round trip).
- **The end.** The port holds the end of the match until its last frame is confirmed. After
  it, and when the game exits (`quit`, for up to three seconds), the inputs keep going until
  the other side has them all. The log has each match's result, its last checksum, rollbacks,
  holds and round trip (measured at frame boundaries, so up to a frame on each side above the
  network's own).

## The replay test

`TGG_NETPLAY_REPLAY=<file.slp>` plays a Slippi replay's match: each game boots the replay's
match, plays one side's recorded inputs as its own (`TGG_NETPLAY_SIDE` picks which of the
replay's two players), gets the other side's over the network, and sets each frame's RNG seed
to the replay's, as Slippi's netplay keeps the two consoles' seeds together. With the port's
replay check comparing (`TGG_REPLAY` and `TGG_REPLAY_CHECK_ONLY=1`), both games must end with the
console's result:

```sh
for side in 0 1; do
  TGG_REPLAY=game.slp TGG_REPLAY_CHECK_ONLY=1 TGG_SYNC_DISC=0 TGG_NETPLAY_REPLAY=game.slp \
  TGG_NETPLAY_SIDE=$side TGG_NETPLAY_PORT=$((52000 + side)) TGG_NETPLAY_PEER=127.0.0.1:$((52001 - side)) \
  TGG_NETPLAY_DELAY=2 TGG_NETPLAY_FAKE=latency=40,jitter=10,loss=5 \
  "$(tgg port path)/tgg-melee" ~/melee.iso &
done; wait
```

Each prints `replay: MATCH` when every frame matched the console, and the mod's summary:
rollbacks, holds, the round trip and how many checksums matched. Only replays of online
matches work: an offline match can pause, and the test can't know a frame's place in the
replay before the frame has run. The replay's Slippi rule mods have to be installed, as for
the replay check.

## Build

```sh
tgg mod build          # packed into build/
tgg mod dev            # build, install and run
```
