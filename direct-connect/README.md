# Direct connect

Play someone over the internet: one of you hosts, the other types the host's address.

## For players

It's a screen on the Mods page (Y on the main menu, then Direct connect). The network side is
`tgg.netplay`: rollback, input delay, and checking the two games agree.

1. Both games need the same game version and the same mods. The simplest way is to install
   only `tgg.netplay` and `tgg.direct-connect`, on both sides.
2. The host picks **Host**. The screen shows the host's addresses, a Tailscale address first
   (in gold) when there is one. The host's port (51000 unless changed) must be reachable: let
   UDP 51000 through the firewall (Windows asks the first time you host: allow it), or have the
   other player host.
3. The other player picks **Join**, types the address with the on-screen keyboard (or a real
   keyboard) and presses Start. The port is added if you leave it out. Next time,
   **Join &lt;last address&gt;** is right there: press A.
4. The lobby: the character grid on top, both players below, the stage in the middle.
   - A picks the character under the cursor, X and Y change its color.
   - The host changes the stage with L and R.
   - Start says you're ready. It can't be taken back; B leaves instead.
   - When both are ready, the match starts on both games.
5. After GAME! you're both back in the lobby. Press Start again for a rematch.

Nobody can pause a match over the network, as on Slippi. B in the lobby leaves, and the other
player sees that you left. If the screen says the other game has other mods, compare the two
mods folders; the log lists each side's mods.

Your controller plays your side's port whichever port it's plugged into: the host is port 1
(red), the other player port 2 (blue). With more than one controller plugged in, the first
connected one is used, or pick one with netplay's "Your controller" setting.

### Settings

All of these are your own and don't need to match the other player's.

| Setting | Default | Does |
|---|---|---|
| Last address joined | empty | what **Join &lt;address&gt;** connects to |
| Port to host on | 51000 | the UDP port the host listens on |
| Stocks (host) | 4 | the match's stocks, when you host |
| Time limit, minutes (host) | 8 | the match's time limit when you host, 0 for none |
| Stage (host) | Battlefield | the stage the lobby starts on, when you host |
| Character, Color | Fox, 0 | your last pick |

To skip the menus, start the game with `TGG_DIRECT_CONNECT=host` or
`TGG_DIRECT_CONNECT=join:<address>`: it opens this screen and hosts or joins at once.

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

`tgg.netplay` 0.3 (`"depends": { "tgg.netplay": "^0.3" }`), and tgg-melee 0.1.0-rc.9 or later
(the first with `tgg/net.h`).

## Build

```sh
tgg mod build    # packs build/tgg.direct-connect-<version>.zip
tgg mod dev      # build, install and run the game
```
