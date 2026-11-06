# Netplay

Rollback netplay for tgg-melee: two games play a match over UDP. Each game runs ahead on a
guess of the other player's input, and when the real input arrives and differs, it rolls back
to that frame and runs the frames since again (the port's [rollback API](https://textures.gg/docs/mods/latest/rollback)).

There's no lobby yet: both games start straight into a match (`TGG_BOOT_MATCH`), and the
settings are environment variables.

```sh
# this machine, side 0, port 1
TGG_NETPLAY_PEER=192.168.1.20:51000 TGG_NETPLAY_SIDE=0 TGG_BOOT_MATCH=fox,marth,31 tgg mod dev
# the other machine, side 1, port 2
TGG_NETPLAY_PEER=192.168.1.10:51000 TGG_NETPLAY_SIDE=1 TGG_BOOT_MATCH=fox,marth,31 tgg mod dev
```

| Variable | Meaning |
|---|---|
| `TGG_NETPLAY_PEER` | the other game, `host:port`; netplay is off without it |
| `TGG_NETPLAY_PORT` | the UDP port this game listens on, default 51000 |
| `TGG_NETPLAY_SIDE` | 0 or 1, one of each: side 0 picks the match's seed |
| `TGG_NETPLAY_PLAYERS` | the controller ports side 0 and side 1 play, default `1,2`. Your own controller is read from your side's port |
| `TGG_NETPLAY_DELAY` | frames of input delay, default 2 |
| `TGG_NETPLAY_ROLLBACK` | the most frames to roll back, default 8 (up to 30) |
| `TGG_NETPLAY_FAKE` | a fake network for tests: `latency=40,jitter=10,loss=5` (one way, ms and percent) |
| `TGG_NETPLAY_REPLAY` | the replay test: play a Slippi replay's match, below |

Both games must run the same build of the game and the same mods; the log says so when they
meet, and the game stops when they don't.

## How it works

```text
src/mod.c         the handshake, inputs, rollback, confirming frames, checksums
src/wire.c        what goes over the network: inputs and packets, byte by byte
src/net.c         the UDP socket (POSIX); the one file Winsock would replace
src/fakenet.c     the fake network: latency, jitter, loss, reordering
src/replaytest.c  the replay test
src/slp.c         reading a Slippi replay (the replay player's reader)
```

- **Handshake.** Before the match, the `advance` event holds the game and says hello every
  100 ms until the other side's hello arrives: the game build (`tgg_game_build`), the mod-set
  id (`tgg_mod_set_id`), the side and side 0's seed. Once the match is set up, every packet
  also carries a hash of the match's setup, and a different one stops the game.
- **Inputs.** Your controller is read in `pad_read` when a frame first runs and plays
  `TGG_NETPLAY_DELAY` frames later. An input is the controller's report and the state the game
  made of it, so the other game sets both exactly as yours did. Every packet carries every
  input the other side hasn't acknowledged (up to 40), so a lost packet costs only time.
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
- **The end.** The port holds the end of the match until its last frame is confirmed. When
  the game exits (`quit`), it keeps sending for up to three seconds, until the other side has
  every input.

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
