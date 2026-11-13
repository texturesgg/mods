# Changelog

## 0.7.0

- Games that number their fighters differently say so when they meet, instead of playing the wrong fighter. Needs tgg-melee API 0.6; doesn't play with 0.6.x.

## 0.6.1

- A rematch after a boot match no longer hangs: both games count it as match 2.

## 0.6.0

- Two to four games in a session.
- A relay for games that can't punch through to each other.
- Says on screen when the games stop matching.
- Every game in a session plays with everything unlocked; your own save keeps what it had.
- netplay_rtt, the round trip to another game, and the match's seed for the port.
- TGG_NETPLAY_NO_TAILSCALE leaves Tailscale addresses out.
