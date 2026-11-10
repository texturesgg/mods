# Netplay

Rollback netplay for tgg-melee: two to four games play matches together over UDP, on Linux,
macOS and Windows, and games on different ones play each other.

## For players

Netplay has no screen of its own. Install a connect mod with it, like `tgg.direct-connect`
(host, or type the host's address), and that mod opens the session and starts the matches.

Each game runs ahead on a guess of the other players' input. When the real input arrives and
differs, the game rolls back to that frame and runs the frames since again. Matches played
offline are untouched: rollback is on only while a session is open.

Settings, on the Mods page. They're your own and don't have to match the other players'.

| Setting | Default | Does |
|---|---|---|
| Input delay (frames) | 2 | frames between pressing a button and it happening, 0 to 15. More delay means fewer rollbacks |
| Your controller | Any | the controller you play with: Any (the first one plugged in) or Port 1 to 4. It plays your side's port wherever it's plugged in |
| When hosting, mods that must match | Gameplay mods | Gameplay mods: display mods (skins, HUDs, the replay recorder) can differ. All mods: every mod has to match, display mods too, for tournaments and the like. The host's choice counts |

All the games need the same game and the same gameplay mods. They check when they meet, and the
session ends if they differ. On one platform that's the same build and mod-set id. Between
platforms (Linux, macOS and Windows) it's the same game version and the same mod versions,
built from the same reviewed source. Display mods, the ones textures.gg's review marked as
only changing what you see, can differ unless the host wants every mod to match. If a match
stops agreeing, the log names the display mods only one of you has: the likely cause.

## The session

One game hosts and up to three join it. Once the host locks the session, this mod is the
port's session (`tgg/session.h`): shared screens run in lockstep on the menu input every game
sends, `tgg_session_rand` starts from the seed the host picked, and `tgg_play_match` starts
the match through `netplay_play`. A mod with shared screens gets netplay from that alone and
doesn't depend on this mod.

## For mod makers

### The API

A connect mod (a direct-connect screen, a lobby, a matchmaker) opens a session, talks to the
other games with messages, locks the session once everyone's in, and starts a match or a
shared screen. Netplay does the rest: inputs, guessing, rollback, pacing and desync checks.
List it in your manifest and include `include/netplay.h` (copy it, or add netplay's
`include/` to the include path):

```json
"depends": { "tgg.netplay": "^0.5" }
```

| Function | Does |
|---|---|
| `netplay_open(side, peer, port, &why)` | opens a session. Side 0 hosts: with `peer` NULL it waits on `port` and takes up to three games. Any other side joins the host at `peer` ("host:port") |
| `netplay_status(&info)` | the state (waiting, connecting, connected, starting, playing, failed), this game's side, how many games are in, whether it's locked, the round trip, why it failed |
| `netplay_lock()` | the host: no more games join, and the session (`tgg/session.h`) starts on every game. The first `netplay_play` locks too |
| `netplay_send(data, len)`, `netplay_receive(buf, cap, &from)` | messages to every other game, up to 400 bytes, whole, once and in order, with the side that sent each |
| `netplay_play(data)` | from a screen: the host passes the match (`StartMeleeData`, one human player per game, in side order), joiners pass NULL to take it. All leave for the match together and come back after |
| `netplay_close()` | ends the session and tells the other games. When any game leaves, the session ends for all |

Call these from presentation code (a screen's functions or a PRESENT event), never from
simulation code. `tgg.direct-connect` is a complete example of a mod built on them.

### How it works

```text
src/session.c     the session: hello, keeping in touch, messages, agreeing on a match; the API
src/match.c       a match: inputs, rollback, confirming frames, checksums, pacing
src/wire.c        the packets, byte by byte
src/net.c         the UDP socket and its peers, over the port's tgg/net.h
src/fakenet.c     a fake network for testing: latency, jitter, loss, reordering
src/replaytest.c  the replay test
src/slp.c         reading a Slippi replay
```

- **A star.** Each joiner talks to the host only. The host passes on what one joiner sends
  the others: its messages, its menu input on shared screens, and its match inputs. Side 0
  hosts; joiners get sides 1 to 3 in the order the host took them.
- **Hello.** A joiner says hello every 100 ms until the host has its hello: the game build,
  the platform, the mod-set ids. The host takes each game whose hello arrives with the same
  build and mods, gives it its side, and tells every game how many are in. After the lock it
  turns new games away.
- **Between matches.** A lobby packet on each link every 100 ms carries the round trip,
  unacknowledged messages, and which of the last match's inputs this game has. Ten seconds
  of silence from any game ends the session.
- **A match.** The host's `netplay_play` sends the match number, a new seed and the setup as
  the console lays it out (`tgg_match_to_console`); each joiner's says it's ready. A joiner
  starts once it has the match, the host once every joiner is ready, and each holds the
  first frame until a packet of the match has come from every game it talks to. Every packet
  carries a hash of the setup, and a mismatch ends the session.
- **Inputs** (`pad_read` event). Your controller is read when a frame first runs and plays the
  input delay later, in your side's port. An input is the controller's report and the state
  the game made of it, so the other games set both exactly. Every packet carries every input
  the other end hasn't acknowledged (up to 40, shared between the sides it carries), so a
  lost packet costs only time.
- **Rollback** (`advance` event). Each frame gets each other side's input if it has arrived,
  else the last one that did. When a real input differs from the guess, `tgg_rollback` goes
  back to that frame.
- **Confirming.** A frame is confirmed once every input is known and it has run on them
  (`tgg_rollback_confirm`). Packets carry a confirmed frame's `tgg_frame_checksum`: joiners
  compare with the host's and the host with each joiner's, and the first frame that differs
  is logged as a desync.
- **Pacing.** A frame is held when running it would guess further ahead than a rollback
  reaches, and once in ten frames when this game is more than a frame ahead of the slowest
  game it talks to.
- **The end** (`match_end`, `quit` events). The port holds the end of the match until its
  last frame is confirmed, and inputs keep going until every game has them all. The log has
  each match's result, last checksum, rollbacks, holds and round trip.

### Without a connect mod

Environment variables open a session for a match the boot sets up (`TGG_BOOT_MATCH`), or,
without one, a plain session for a mod to play in (a shared screen):

```sh
# the host, side 0, port 1: waits for three more games
TGG_NETPLAY_GAMES=4 TGG_BOOT_MATCH=fox,marth,31,falco,peach tgg mod dev
# each joiner: sides 1 to 3, ports 2 to 4, in the order they join
TGG_NETPLAY_PEER=192.168.1.10:51000 TGG_NETPLAY_SIDE=1 TGG_BOOT_MATCH=fox,marth,31,falco,peach tgg mod dev
```

| Variable | Meaning |
|---|---|
| `TGG_NETPLAY_GAMES` | the host: how many games the session has, 2 to 4, default 2. It locks once they're in |
| `TGG_NETPLAY_PEER` | a joiner: the host, `host:port`. A host may say hello to its one joiner too |
| `TGG_NETPLAY_PORT` | the UDP port this game listens on, default 51000 |
| `TGG_NETPLAY_SIDE` | 0 hosts (and picks the match's seed), 1 joins |
| `TGG_NETPLAY_PLAYERS` | the ports each side plays, in side order, default `1,2,3,4` |
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

tgg-melee 0.1.0-rc.12 or later: netplay provides the port's session (`tgg/session.h`), which
older builds don't have.

## Build

```sh
tgg mod build    # packs build/tgg.netplay-<version>.zip
tgg mod dev      # build, install and run the game
```
