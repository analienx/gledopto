# GL-SD-301P health snapshot fixtures (v2, 48 bytes, little-endian)

Decoded by `tools/glsd301p_health_decode.py`, exercised by
`tools/tests/test_glsd301p_health_decode.py` and mirrored by the C vectors
in `tests/test_glsd301p_health.c`.

Each valid fixture carries `hex` (exactly 48 bytes) plus the full decoded
`expected` mapping. Invalid fixtures carry `expect_error` with a substring
of the decoder's rejection message.

| Fixture | Meaning |
| --- | --- |
| `boot_defaults.json` | Fresh boot: disconnected, no flags, zero counters |
| `joined_active.json` | Joined device with mixed counters (mirrors the C layout test) |
| `saturated.json` | Every saturating source at max; version/flags still exact |
| `joining_retry.json` | Joining with OFF pending, UART busy, rejoin attempts |
| `bad_version.json` | Version 1 payload: rejected |
| `bad_length_short.json` | 47 bytes: rejected |
| `bad_length_long.json` | 49 bytes: rejected |
