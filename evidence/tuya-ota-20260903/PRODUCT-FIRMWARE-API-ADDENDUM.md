# Product-firmware API addendum — 2026-09-03 (order 5531968391)

## Bounded checks performed

- **A (authorization/package name per failing endpoint):** Tuya's OpenAPI
  exposes no endpoint that lists a project's subscribed API products or the
  package attached to a failing route — the response body is only
  `{code: 28841101, msg: "No permissions. This API is not subscribed."}`.
  The API-product subscription list is visible only in the Developer
  Platform UI (user-side capture remains possible but is no longer needed
  for the classification below).
- **B (PID `1jlpstyg` in any authorization list):** the app-account project
  has no product-authorization surface for a third-party PID. The developer
  platform Products section lists only PIDs owned by the account; a
  Gledopto-owned PID is visible only as the device's `product_id` field.
  Tuya's docs (per supervisor) place `/product/hardwares`, `/product/firmwares`
  and `/product/firmware/versions` under the **PID Permission Package /
  Firmware Management** — which requires the product owner (Gledopto) to
  authorize/share the PID to our account.
- **Supporting asymmetry:** the device-level `GET /v2.0/cloud/thing/{id}/firmware`
  (App/WeChat Mini Program Permission Package) succeeds, while all three
  product-level endpoints fail with `28841101` on the same credentials/token.

## Classification (check C)

```text
PRODUCT_FIRMWARE_API = NOT AVAILABLE TO APP-ACCOUNT AUTHORIZATION
REASON = endpoints belong to the vendor-side PID/Firmware Management
  permission package; PID 1jlpstyg is owned by Gledopto (third party) and
  is not authorized to our account; no purchasable subscription on our
  side grants access to a third party's product firmware without vendor
  PID authorization.
PAID_SUBSCRIPTION_PURSUIT = STOPPED (per order — Flagship pricing is
  irrelevant and would not solve third-party PID authorization)
```

## Consequence

The historical/product-firmware cloud lineage cannot be queried without
Gledopto's PID authorization. Remaining fallbacks (per order D):
1. Device-level GETs — cheap; `tuya-watch` re-run will detect any future
   `upgrade_status=1` + URL immediately.
2. Smart Life gateway traffic capture (user-side mitmproxy) if a real
   update is ever offered.
3. Gledopto support request for the exact tuple (letter drafted).
4. SWS/UART flash dump of a sacrificial spare (USB-TTL, ~$1.50 hardware).