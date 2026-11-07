# Direct connect

Play someone over the internet (or a tailnet): one of you hosts, the other types the host's
address, you both pick a character, the host picks the stage, and the match starts on both
games. After it you're both back on the same screen for the next one.

It's a screen on the Mods page (Y on the main menu, then Direct connect). The network side
is [`tgg.netplay`](../netplay): rollback, input delay, and checking the two games agree.

## Playing

1. Both games need the same game version and the same mods (the same versions and settings
   of everything that counts for netplay). The simplest way is a mods folder with just
   `tgg.netplay` and `tgg.direct-connect` in it, the same on both machines.
2. The host picks **Host**. The screen shows this machine's addresses, the tailnet's first
   (gold), and its name. The host's port (51000 unless changed) must be reachable: on a
   machine with a firewall, let UDP 51000 in, or let the other machine host.
3. The other player picks **Join**, types the address with the on-screen keyboard (or the
   real keyboard) and presses Start. The port is added if you leave it out. Next time,
   **Join &lt;last address&gt;** is right there: press A.
4. The lobby: the character grid on top, you and them below, the stage in the middle.
   - A picks the character under the cursor, X and Y change its color.
   - The host changes the stage with L and R.
   - Start says you're ready. It can't be taken back; B leaves instead.
   - When both are ready, the match starts on both games.
5. After GAME! you're both back in the lobby. Press Start again for a rematch.

There's no pausing in a match over the network, as on Slippi. B in the lobby leaves, and the
other player sees that you left.

### Your controller

Your controller plays your side's port whichever port it's plugged into: the host is port 1
(red), the other player port 2 (blue). With more than one controller plugged in, the first
connected one is used; netplay's "Your controller" setting on the Mods page picks one.

## Settings (Mods page)

| Setting | Meaning |
|---|---|
| Last address joined | what **Join &lt;address&gt;** connects to |
| Port to host on | the UDP port the host listens on, default 51000 |
| Stocks (host), Time limit (host), Stage (host) | the host's rules; the other side's don't matter |
| Character, Color | your last pick |
| Netplay: Input delay (frames) | default 2; more delay, fewer rollbacks |
| Netplay: Your controller | Any, or one port |

All of these are the player's own (`TGG_SETTING_*_LOCAL`): they don't need to match between
the two players.

## Playing the desktop against the MacBook

The owner's Linux desktop and MacBook play over Tailscale. Both run the same commit of the
port and the same mods: the desktop the mods in `~/dev/texturesgg/play-mods`, the MacBook a
copy of the same mods built for macOS in `~/dev/saavylab/tgg-melee-macos/play-mods`.

### Once, after the port changes

On the desktop, build the port in `~/dev/texturesgg/tgg-melee-lab` and every mod in
`play-mods` against it (mods built for another layout are refused), netplay and direct
connect included:

```sh
cd ~/dev/texturesgg/tgg-melee-lab
git checkout --detach main
nix develop --command bash -c '
  unset NIX_CFLAGS_LINK
  cmake -S . -B build -G Ninja && ninja -C build dwarf && ninja -C build || exit 1
  for m in netplay direct-connect competitive-rules freeze-dead-up-fall legal-stages \
      neutral-spawns offscreen-damage quick-start replay-browser replay-player \
      replay-recorder skip-results stadium-monitor stadium-preload whispy-blow-dir-fix; do
    b=~/.cache/tgg-play-mods/$m
    cmake -S ~/dev/texturesgg/mods/$m -B $b -G Ninja -DTGG_SDK=$PWD/build/tgg-melee-sdk &&
      cmake --build $b && cp -r $b/mods/* ~/dev/texturesgg/play-mods/ || echo "$m failed"
  done'
```

On the MacBook, the port in `~/dev/saavylab/tgg-melee-macos` and the mods in its `mods/` are
built the same way (the port's README, macOS), and `play-mods/` holds each one's
`build/mods/*`.

### Playing

Desktop:

```sh
cd ~/dev/texturesgg/tgg-melee-lab
TGG_MODS_DIR=~/dev/texturesgg/play-mods build/tgg-melee "$HOME/Games/Slippi/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).iso"
```

MacBook:

```sh
cd ~/dev/saavylab/tgg-melee-macos
TGG_MODS_DIR=$PWD/play-mods build/tgg-melee "iso/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).iso"
```

Then:

1. On the MacBook: Y on the main menu, **Direct connect**, **Host**. The first address, in
   gold, is the tailnet's: `100.73.221.52`. (If macOS asks whether tgg-melee may accept
   incoming connections, allow it.)
2. On the desktop: Y, **Direct connect**, **Join**, type `100.73.221.52` and press Start.
   Next time it's **Join 100.73.221.52**: press A.
3. Both: pick a character with A (X and Y change the color). The MacBook picks the stage
   with L and R. Start when ready; the match starts when both are.
4. After the match you're both back in the lobby: Start again for a rematch. B leaves.

The MacBook hosts because the desktop's firewall doesn't let UDP in from the tailnet. For the
desktop to host, allow it in the NixOS config (`networking.firewall.trustedInterfaces = [
"tailscale0" ];`). That also lets Tailscale connect the two directly: without it, the
packets go through a Tailscale relay (`tailscale ping jaysons-macbook-pro` says "via DERP"),
which adds tens of milliseconds each way.

If the screen says the other game has other mods, the two `play-mods` folders differ, or a
mod's setting does (every setting counts but the ones marked as the player's own). The log
lists each side's mods.

To skip the menus, start either game with `TGG_DIRECT_CONNECT=host` or
`TGG_DIRECT_CONNECT=join:<address>`: it opens this screen and hosts or joins at once.

## Build

```sh
cmake -S . -B build -G Ninja -DTGG_SDK=$(tgg sdk path)
cmake --build build
```

The mod lands in `build/mods/tgg.direct-connect/`. It needs `tgg.netplay` 0.2 installed
beside it.
