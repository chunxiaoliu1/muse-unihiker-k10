# SDK credential module

`sdk_credentials.c` owns the boot cache, USB validation/queueing, and NVS save/readback/clear operations. `sdk_token_format.h` matches compile-time validation. SDK Tokens are stored separately from device and refresh tokens.

Startup order: `config_store_init()` -> `identity_init()` -> `sdk_credentials_init()`. `homehub/sdk_token` takes priority. Public builds use `CONFIG_GADGET_SDK_RUNTIME_ONLY=y`, disabling a compiled-in SDK Token and personal-token fallback. Active token memory stays fixed until restart, so pairing may safely retain its pointer.

Muse submits serial commands through `muse_link_ops_t.sdk_command`, without a reverse dependency on main. The serial task performs no NVS I/O. The internal-stack keeper in `muse_glue.c` calls `sdk_credentials_process()` under the same operation gate used by pairing and reset. Future BLE or hotspot configuration should reuse this queue and persistence behavior, never access NVS from an external-memory stack.

[USB configuration and protocol](../../tools/muse/usb-config/README.md).

Tests: `node --test tests/test_usb_config.mjs` and `python -m unittest discover -s tests -p test_sdk_credentials.py -v`. The latter needs a host C compiler and downloaded cJSON sources.
