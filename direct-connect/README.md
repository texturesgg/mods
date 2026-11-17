# Direct connect

Play online. Find a match pairs you with someone who has the same gameplay mods, or play with
friends: one of you makes a room and the others join with its code, or join by address on the
same network.

## For players

It's Online on the main menu. `tgg.netplay` does the netplay itself: rollback, input delay, and
checking everyone's games agree.

- **Find a match** pairs you with someone who has the same gameplay mods as you. Pick a mode
  (VS, 1 on 1, or a mode from a mod that plays online), pick your character, and it searches.
  It shows how many are searching. The first time, it says you might not know the other player
  and that they can see your IP address.
- **Make a room** gets a 4-letter code. Give your friends the code. A room takes as many games as
  netplay plays together (4 today), counting yours.
- **Join a room** asks for the code. Type it with the on-screen keyboard or a real keyboard.
- **Host by address** and **Join by address** skip textures.gg and connect by IP, for games on
  the same network (or Tailscale). The host's port is 51000 unless you change it, and it has to
  get through the firewall. Windows asks the first time you host, so allow it.

Rooms need you signed in to textures.gg. The first time, the screen shows a link and a code to
confirm on your phone or computer.

Everyone needs the same game version and the same gameplay mods. If the host has mods you
don't, the game offers to download them and restart into the room.

Once connected, everyone picks on the game's own character select and the host picks the stage.
Nobody can pause a match over the network, same as Slippi. If two games can't reach each other
directly, they go through a relay and the screen says so.

In a room:

- Each game that joins connects to the host. The room shows who's in, and the host starts
  when everyone's there. Once the host starts, the room takes nobody new until that session
  ends.
- The host leads the menus: the rules (stocks or time, teams or free-for-all, items) and the
  stage are the host's, and everyone picks their own character and team color on the
  character select.
- If anyone leaves or loses the connection, in the menus or in a match, the session ends for
  everyone. The match being played doesn't count. The room stays, the others connect to the
  host again by themselves, and the room says who left. The one who left can join again with
  the code.
- If the host leaves, the room closes.
- A room closes 30 minutes after the host's game drops off textures.gg, and after 12 hours either
  way.
- If one game can't reach the host directly, the room uses a relay for everyone the host meets.
  If the first game reached the host directly and a later one can't, that one can't join.

In Find a match, VS is 4 stocks, 8 minutes, items off, on a random legal stage each game
(Battlefield, Final Destination, Dream Land, Yoshi's Story, Fountain of Dreams, Pokemon
Stadium), the way Slippi's unranked picks them: against the same person, no stage comes up
again until you've played them all. After a match you can play the same person again or find someone else. If the two
games can't connect, you both go back to searching and won't be paired with each other again.

Your controller plays your side whichever port it's plugged into. With more than one plugged in
it uses the first one, or you can pick one with netplay's "Your controller" setting.

### Settings

These are your own and don't need to match anyone else's.

| Setting | Default | Does |
|---|---|---|
| Last address joined | empty | what Join by address offers first |
| Port to host on | 51000 | the UDP port you host on |

To skip the menus, start the game with `TGG_DIRECT_CONNECT=host`,
`TGG_DIRECT_CONNECT=join:<address>`, `TGG_DIRECT_CONNECT=room` (make a room) or
`TGG_DIRECT_CONNECT=room:<code>` (join one), or
`TGG_DIRECT_CONNECT=search[:<mode>[:<character>[:<costume>]]]` (find a match; the mode is `vs`
or a shared menu entry like `main/first-to-3`, the character like `melee/fox`). It opens Online
and does that right away. With `search`, `TGG_DIRECT_CONNECT_YES=1` accepts every prompt and
`TGG_DIRECT_CONNECT_MATCHES=<n>` stops after n matches, for scripted tests.

## For apps

### Starting the game into a room

To make a room, start the game with

```sh
TGG_DIRECT_CONNECT=room TGG_DIRECT_CONNECT_REPORT=<file> tgg-melee <disc image>
```

and to join one, `TGG_DIRECT_CONNECT=room:<code>` with the same report variable. The game opens
Online and makes or joins the room right away. With `TGG_DIRECT_CONNECT_REPORT` set, it appends
one JSON line to that file each time the room changes. The last line is where things are:

```json
{"state":"waiting","code":"ABCD","link":"textures-gg://join/ABCD","host":"saavy","players":["saavy","bo"],"max":4,"signin":{"url":"","code":""},"why":""}
```

| state | means |
|---|---|
| `starting` | signing in and reaching textures.gg |
| `signing-in` | nobody's signed in: show `signin.url` and `signin.code` to confirm there |
| `waiting` | in the room, before the host starts. `code` and `link` are ready to share |
| `getting-mods` | a joiner is getting the host's mods. It restarts with them and joins again by itself, appending to the same file |
| `playing` | the host started |
| `ended` | left the room or it closed. `why` says why, in words for the player |

Every line has every field, `""` or `[]` when it doesn't apply. `players` is everyone in the
room, the host first; `max` is the most games the room takes. A line is written whole, so
read the file's last line and skip one that doesn't parse. Start each run with a new or empty
file. The game never deletes it.

The link is `textures-gg://join/<code>`. Opening it should start the game with
`TGG_DIRECT_CONNECT=room:<code>`.

### Played with

After each session through textures.gg (a room, or a match from Find a match) where at least
one match was played to the end, the game appends one line to `played-with.jsonl` in its data
folder (`tgg_data_dir()`, beside the saves):

```json
{"at":"2026-11-17T01:02:03Z","with":["saavy","bo"],"via":"room"}
```

- `at`: when the session ended, UTC.
- `with`: the other players' textures.gg names, as many as there were.
- `via`: `"room"`, or the queue's name (`"Competitive"`, `"Find a match"`).

It's written once, when the session ends, not after each match. Names only, never addresses.
Past 200 lines the game cuts the file to its last 100, writing a new file and renaming it over
the old one. Read the last lines and skip any that don't parse.

## For mod makers

A complete connect mod on `tgg.netplay`'s API. Everything it does is presentation: the match
itself is the `StartMeleeData` the host builds, and netplay hands it to every game.

- `src/connect.c`: the screen (`TGG_SCREEN`) and the lobby. It opens the session with
  `netplay_open`, follows it with `netplay_status`, and starts each match with `netplay_play`.
  The screens talk in short text messages over `netplay_send` / `netplay_receive`:
  `pick <ckind> <costume> <ready>` for each side's character and
  `stage <stkind index> <stocks> <minutes>` for the host's rules. For
  `TGG_DIRECT_CONNECT`, `TGG_INIT` boots to the menu (`tgg_boot_mode`) and a `frame` event
  opens the screen (`tgg_screen_open`).
- `src/queue.c`: Find a match. `meet.c` sends `search` to the meeting point and gets the
  opponent back as a `meet`, the same as a room. The host (side 0) builds each match and draws
  its stage as Slippi's `getRandomStage` does (`draw_stage`: a bag of the legal stages, new
  for each opponent). Modes other than VS come from `tgg_menu_shared`; the host calls
  `netplay_lock` and `tgg_menu_pick`, and every game opens it. The screens talk with `qpick`
  and `qagain` messages.
- `src/meet.c`: the meeting point. A room is a star: the host opens one session, and each game
  that joins gets a `meet` with the host alone. The host adds each later one with
  `netplay_add_candidates`, and sends `room.lock` once netplay locks. A joiner whose session
  ended takes the room's next `meet` and connects again (`meet_room_again`).
- `src/played.c`: Played with. Each game sends `tgg-name <name>` once its session locks, and
  the line is written as the session closes (`dc_netplay_close`).
- `src/report.c`: the room report for apps (`TGG_DIRECT_CONNECT_REPORT`).
- `src/roomplay.c`: scripted room matches for tests. With `TGG_DIRECT_CONNECT_YES=1` and
  `TGG_DIRECT_CONNECT_TIME=<seconds>`, a room's games skip the menus and play
  `TGG_DIRECT_CONNECT_MATCHES` time matches (`TGG_DIRECT_CONNECT_PICK=<character>[:<costume>]`
  for each game's pick), then leave. `TGG_DIRECT_CONNECT_GAMES=<n>` has the host start once n
  games are in.
- `src/keyboard.c`: an on-screen keyboard for a controller, plus the real keyboard
  (`tgg/input.h`).
- `src/draw.c`: the screen in the menus' look, with `tgg/draw.h`.
- `src/addr.c`: this machine's IPv4 addresses, Tailscale's first, from the port's `tgg/net.h`
  (`tgg_net_interfaces`): an address in Tailscale's range on an interface named for it, or
  alone on its interface, counts as Tailscale's. No file has platform calls, so the mod builds
  unchanged for Linux, macOS and Windows.
- `src/names.c`: the character grid in the character select's order, and the stages.
- `src/netplay.h`: a copy of `tgg.netplay`'s `include/netplay.h`.

Settings use `TGG_SETTING_*_LOCAL`, so they're the player's own and stay out of the netplay
check.

## Requirements

`tgg.netplay` 1.0 (`"depends": { "tgg.netplay": "^1.0" }`), `tgg.http` 1.0, and mod API 1.0
(the first with `tgg_menu_shared`).

## Build

```sh
tgg mod build    # packs build/tgg.direct-connect-<version>.zip
tgg mod dev      # build, install and run the game
```
