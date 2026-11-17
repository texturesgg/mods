# Direct connect

Play online. Find a match pairs you with someone who has the same gameplay mods, or play with up
to three friends: one of you makes a room and the others join with its code, or join by address
on the same network.

## For players

It's Online on the main menu. `tgg.netplay` does the netplay itself: rollback, input delay, and
checking everyone's games agree.

- **Find a match** pairs you with someone who has the same gameplay mods as you. Pick a mode
  (VS, 1 on 1, or a mode from a mod that plays online), pick your character, and it searches.
  It shows how many are searching. The first time, it says you might not know the other player
  and that they can see your IP address.
- **Make a room** gets a 4-letter code. Give your friends the code.
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

## For mod makers

A complete connect mod on `tgg.netplay`'s API. Everything it does is presentation: the match
itself is the `StartMeleeData` the host builds, and netplay hands it to both games.

- `src/connect.c`: the screen (`TGG_SCREEN`) and the lobby. It opens the session with
  `netplay_open`, follows it with `netplay_status`, and starts each match with `netplay_play`.
  The two screens talk in short text messages over `netplay_send` / `netplay_receive`:
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

`tgg.netplay` 0.10 (`"depends": { "tgg.netplay": "^0.10" }`), `tgg.http` 0.1, and mod API 0.10
(the first with `tgg_menu_shared`).

## Build

```sh
tgg mod build    # packs build/tgg.direct-connect-<version>.zip
tgg mod dev      # build, install and run the game
```
