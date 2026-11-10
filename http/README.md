# HTTP

HTTPS requests and WebSockets for other tgg-melee mods: API calls to a web service, a live
connection to a server. TLS is checked against the computer's own root certificates.

## For players

Nothing to set up. Mods that talk to the internet (Online's room codes, for one) install it
with them.

## For mod makers

List it in your manifest and include `include/http.h` (copy it, or add this mod's
`include/` to the include path):

```json
"depends": { "tgg.http": "^0.1" }
```

```c
#include "http.h"

static http_request* req;

/* Somewhere in a screen or a frame event */
req = http_request_start("GET", "https://api.textures.gg/", NULL, NULL, 0);

/* Each frame after */
if (req != NULL && http_request_poll(req) != HTTP_WORKING) {
    size_t len;
    const char* body = http_response_body(req, &len); /* NULL when it failed */
    tgg_log("%d: %s", http_response_status(req), body != NULL ? body : http_request_error(req));
    http_request_free(req);
    req = NULL;
}
```

| Function | Does |
|---|---|
| `http_request_start(method, url, headers, body, len)` | starts a request. `headers` is more header lines, `"Name: value"` with `\r\n` between, or NULL |
| `http_request_poll(r)` | `HTTP_WORKING`, `HTTP_DONE` or `HTTP_FAILED` |
| `http_response_status(r)`, `http_response_body(r, &len)` | the answer, once done |
| `http_request_error(r)` | why it failed, in words for the player |
| `http_request_free(r)` | stops it if it's going, frees it |
| `http_ws_open(url, headers)` | opens a WebSocket (`wss://`) |
| `http_ws_state(ws)` | `HTTP_WS_CONNECTING`, `HTTP_WS_OPEN` or `HTTP_WS_CLOSED` |
| `http_ws_send(ws, text, len)` | sends one text message once it's open |
| `http_ws_receive(ws, buf, cap)` | the next whole message, or 0 when none is waiting |
| `http_ws_error(ws)` | why it closed |
| `http_ws_close(ws)` | closes it and frees it |

- Nothing waits for the network. Everything moves forward each frame and whenever you call
  in. The one exception is looking up a host's name when a connection starts, which can
  take a moment.
- `https://` and `wss://` go anywhere. Plain `http://` and `ws://` only go to the local
  network (a router, another computer at home), never the internet.
- A response body can be up to 8 MB, a WebSocket message up to 1 MB. Requests send
  `Connection: close`: one connection per request.
- WebSockets send a ping every 30 seconds, so a quiet connection stays open, and answer the
  server's pings.
- Presentation code only, like the port's `tgg/net.h` it runs on.
- Every connection counts toward the port's 16 sockets per mod, and they're all this mod's,
  so all mods using it share those 16.

### How it works

```text
src/conn.c      a connection: TCP from the port (tgg/net.h), and TLS on top
src/request.c   requests: HTTP/1.1, Content-Length or chunked
src/ws.c        WebSockets: the upgrade, frames, pings
src/mbedtls/    Mbed TLS 3.6.7, the TLS library, unchanged
include/        http.h, and Mbed TLS's headers
```

TLS is [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) 3.6 (Apache-2.0), compiled into the
mod from source. A mod only links the C runtime, so what Mbed TLS would ask the system for
comes from the port instead:

- random numbers: `tgg_net_random` (`MBEDTLS_ENTROPY_HARDWARE_ALT`)
- the root certificates: `tgg_net_trust_roots` (Linux and macOS: the system's bundle file;
  Windows: the system's root store)
- the clock: `tgg_net_time_ms` (`MBEDTLS_PLATFORM_MS_TIME_ALT`)
- the network: `tgg_net_connect`, `tgg_net_read` and `tgg_net_write`

`include/mbedtls/mbedtls_config.h` is Mbed TLS's default configuration with those
changes, each marked `tgg.http`, and with the server side, files, sockets and timers turned
off. To update Mbed TLS, replace `src/mbedtls/` with the release's `library/`, and
`include/mbedtls/` and `include/psa/` with its `include/`, then make the same changes to
the configuration again.

## Requirements

tgg-melee with mod API 0.4 or later (`tgg_net_connect` and the rest).

## Build

```sh
tgg mod build    # packs build/tgg.http-<version>.zip
```
