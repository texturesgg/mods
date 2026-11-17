# Changelog

## 0.10.0

- The replay test reads damaged replay files safely, as tgg.replay-player 0.7.0 does.
- `netplay_nat` says what the router does (easy, hard or unknown), from asking STUN on two ports, so the meeting point can pair players whose games can reach each other. `netplay_candidates` waits for both answers (at most two seconds).

## 0.9.0

- Games with different rules (stocks, time, items, damage ratio, speed) won't start a match together, and say why, instead of desyncing. Can't play against 0.8.x.

## 0.8.0

- A desync says what differed: the fighters, the items, the RNG, or a mod's state by its id. Needs tgg-melee API 0.9 (`tgg_frame_checksum_parts`). Can't play against 0.7.x.

## 0.7.0

- Games that number their fighters differently say so when they meet, instead of playing the wrong fighter. Needs tgg-melee API 0.6. Can't play against 0.6.x.

## 0.6.1

- A rematch after a boot match no longer hangs: both games count it as match 2.

## 0.6.0

- Two to four games in a session.
- A relay for games that can't punch through to each other.
- Says on screen when the games stop matching.
- Every game in a session plays with everything unlocked. Your own save keeps what it had.
- netplay_rtt, the round trip to another game, and the match's seed for the port.
- TGG_NETPLAY_NO_TAILSCALE leaves Tailscale addresses out.
