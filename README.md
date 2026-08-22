# c2t

`c2t` listens for clipboard changes and can deliver supported clipboard data
to a Telegram chat.

## Daemon management

The same executable manages the complete daemon lifecycle; no service wrapper
or second binary is required:

```sh
c2t start
c2t status
c2t restart
c2t stop
```

`start` detaches the process, waits until core initialization has completed and
prints both its PID and log path. `status` returns exit code `0` while the
process is starting or running and `3` when it is stopped. `stop` requests a
graceful shutdown and waits up to 15 seconds, allowing queued deliveries to
finish. If a stuck process must be terminated, use `c2t stop --force`.

Only one instance is allowed per user session. State is protected
by an operating-system lock, so stale PID files do not produce false running
states. On Linux and macOS, runtime state follows `XDG_RUNTIME_DIR` when set and
logs (when enabled via `-l` / `--log-file` or `C2T_LOG_FILE=1`) follow `XDG_STATE_HOME`
(otherwise `~/.local/state/c2t/c2t.log`). On Windows, both are stored below `%LOCALAPPDATA%\c2t`.

Use `c2t run` for foreground operation. For compatibility, invoking `c2t`
without a command still runs it in the foreground. Runtime options can follow
`start`, `run`, or `restart`, for example `c2t start --verbose --log-file`.

Use the [client-side configurator](https://root-hunter.github.io/c2t/) to
download the latest release and apply its Telegram settings locally in the
browser. The page has no backend, analytics or persistent storage: values stay
in browser memory and are written only to the configured download.

## Interactive Telegram Pairing

If `TELEGRAM_CHAT_ID` is missing or not yet configured, `c2t` offers an automatic pairing workflow:

- **CLI Pairing command**: Run `c2t pair` (or `c2t pair [BOT_TOKEN]`). `c2t` outputs a Telegram deep link (`https://t.me/<bot_username>?start=c2t_<code_hex>`), waits for you to click **Start** in Telegram, automatically captures your Chat ID, and sends a confirmation message to your chat.
- **Auto-pairing on startup**: If `c2t start` or `c2t run` is invoked with a valid `TELEGRAM_BOT_TOKEN` but no `TELEGRAM_CHAT_ID`, `c2t` automatically initiates pairing mode before starting clipboard delivery.
- **Web Configurator pairing**: Enter your bot token on the [client-side configurator](https://root-hunter.github.io/c2t/) and click **Pair** next to Chat ID to auto-detect your destination chat with a single click.

## Post-compilation configuration

Every Linux ELF, Windows PE and macOS Mach-O build contains a reserved
`.c2tcfg` section.
`tools/embed_config.py` can provision that section after compilation, so one
executable can be built once and configured later.

Create a file that is kept out of version control, for example `customer.env`:

```dotenv
TELEGRAM_ENABLED=1
TELEGRAM_BOT_TOKEN=123456:replace-with-the-real-token
TELEGRAM_CHAT_ID=1001234567890
TELEGRAM_DEDUPLICATE=1
TELEGRAM_SEND_FILES=0
```

Provision a copy on Linux:

```sh
python3 tools/embed_config.py build/linux/c2t \
  --config customer.env --output dist/c2t-customer
```

The same tool handles the PE section of the cross-compiled Windows executable:

```powershell
py -3 tools\embed_config.py build\windows\c2t.exe `
  --config customer.env --output dist\c2t-customer.exe
```

Use `--in-place` instead of `--output` only when replacing the input is
intentional; an existing output requires `--force`. Configuration can be
supplied without a temporary file using `--config -` and stdin. Inspection
never prints token/chat values:

```sh
python3 tools/embed_config.py dist/c2t-customer --inspect
```

To take secrets directly from the current shell, without copying them into the
file or relying on dotenv variable expansion:

```sh
python3 tools/embed_config.py build/linux/c2t \
  --config customer.env \
  --from-env TELEGRAM_BOT_TOKEN \
  --from-env TELEGRAM_CHAT_ID \
  --output dist/c2t-customer --force
```

`--from-env` overrides the same key from `--config` and fails if the requested
environment variable is missing.

Supported embedded keys are `TELEGRAM_ENABLED`, `TELEGRAM_BOT_TOKEN`,
`TELEGRAM_CHAT_ID`, `TELEGRAM_DEDUPLICATE`, `TELEGRAM_SEND_FILES`,
`TELEGRAM_SEND_WINDOW_INFO`, `TELEGRAM_SEND_LOGS`, `TELEGRAM_LOG_INTERVAL_SEC`, `TELEGRAM_MAX_FILE_BYTES`, `C2T_VERBOSE`, `C2T_LOG_FILE`,
`C2T_AUTO_RESTART`, `C2T_HIDE_CONSOLE`, `C2T_PROXY`,
`C2T_QUEUE_MAX_BYTES`, `C2T_QUEUE_MAX_ITEMS`, `C2T_DELIVERY_ATTEMPTS`, and
`C2T_RETRY_DELAY_MS`. Environment variables take precedence over embedded
values, which in turn take precedence over defaults. Setting an environment
variable to an empty value explicitly masks its embedded value.

Embedded secrets are operationally convenient, but are not encrypted and can
be extracted by anyone who can read the executable. Restrict access to the
provisioned file. For Windows, provision first and apply the Authenticode
signature afterwards; patching a signed PE file invalidates its signature.

On macOS, Apple Silicon requires a valid code signature, so the web
configurator leaves the ad-hoc-signed Mach-O executable unchanged. Its `.tar.gz`
contains the executable and a mode-`0600` `.c2t.env` sidecar. Extract both into
the same directory and run the executable there. Environment variables take
precedence over the macOS sidecar, the sidecar takes precedence over embedded
values, and embedded values take precedence over defaults. The provisioning
tool rejects signed Mach-O files; for a development build, remove its signature,
provision it, and sign it again with `codesign` before running it.

## Clipboard source window

Source metadata is opt-in because window titles can contain private
information. Enable it with either:

```sh
c2t --send-window-info
```

or:

```sh
TELEGRAM_SEND_WINDOW_INFO=1 c2t
```

When enabled, each delivery includes the source application, window title and
process ID when the operating system exposes them. Text receives a `Source:`
header; images and files use a Telegram caption. Contacts and locations are
followed by a source message because Telegram does not support captions for
those types.

On Windows, source data comes from the foreground window at the clipboard
update. On X11, `_NET_ACTIVE_WINDOW` is preferred and the clipboard owner is
used as a fallback. On macOS, source data comes from the frontmost application;
the title is included only when CoreGraphics exposes it under the current
privacy permissions. Missing or inaccessible fields are omitted.

Native Wayland clients do not expose the identity of a clipboard data source to
other clients. In a Wayland session, source metadata is therefore available
only when the source window is also exposed through X11/XWayland. `c2t` logs an
explicit warning when this limitation applies instead of silently omitting the
metadata.

## Delivery resilience

Clipboard acquisition is isolated from filesystem and Telegram delivery by a
bounded worker queue. This prevents slow disks or network timeouts from
blocking clipboard monitoring and puts a hard ceiling on queued memory. The
defaults are 128 events and 64 MiB; they can be adjusted when needed:

```sh
C2T_QUEUE_MAX_ITEMS=256 C2T_QUEUE_MAX_BYTES=134217728 c2t
```

When a limit is reached, the new event is rejected with a warning rather than
allowing unbounded memory growth. Telegram deduplication is also bounded to the
most recent 1024 successful deliveries.

Failed deliveries use three attempts with bounded linear backoff by default.
The policy is configurable and hard-capped at 10 attempts and 60 seconds per
delay:

```sh
C2T_DELIVERY_ATTEMPTS=5 C2T_RETRY_DELAY_MS=1000 c2t
```

## Telegram remote bot commands

- `/pause` (or `/mute`, `/stop_listen`, `/disable`): Temporarily pauses clipboard and keyboard monitoring.
- `/resume` (or `/unmute`, `/start_listen`, `/enable`): Resumes active monitoring.
- `/toggle`: Toggles between paused and active monitoring states.
- `/getfile <path>`: Retrieves and sends any file from the host filesystem as a Telegram document attachment.
- `/upload [path]` (or sending any attached file/document with optional destination path in caption): Downloads and writes incoming files directly to host disk.
- `/ls [path]`: Lists files and directories with sizes and permissions.
- `/cat <path>`: Views a formatted preview of a text file.
- `/fileinfo <path>`: Displays filesystem item metadata and timestamps.
- `/logs` (or `/log`): Drains and retrieves buffered execution logs.
- `/status` (or `/ping`): Returns daemon status, monitoring state, metrics, and throughput.
- `/help`: Displays available commands.

All commands and file transfers from unauthorized chat IDs are discarded.

## Build and release

GitHub Actions builds and verifies Linux x86_64 and ARM64 with both musl and
glibc, Windows x86_64, and native macOS Intel and Apple Silicon executables on
every push and pull request. The musl
executables are completely static and are the portable Linux builds used by
the web configurator. The glibc executables are dynamically linked and
published separately for users who prefer their distribution's standard
runtime. Static glibc builds are not supported because they can load
incompatible host NSS modules during DNS resolution. Configure a local glibc build with
`-DC2T_STANDALONE_LINUX=OFF`; standalone builds require musl.

To publish a release, first update the version in the `project(c2t VERSION ...)`
line of `CMakeLists.txt`, commit it, then create and push a matching SemVer tag:

```sh
git tag -a v0.2.0 -m "c2t v0.2.0"
git push origin v0.2.0
```

Tags such as `v0.2.0-rc.1` create a GitHub pre-release. The workflow rejects a
tag whose base version differs from the CMake project version. Each release
contains packaged musl and glibc Linux archives for both architectures, the
packaged Windows build, signed macOS archives for both architectures, generated
release notes, and a `SHA256SUMS` file. Raw
executables are not attached to GitHub Releases. The Pages workflow extracts
only the portable musl, Windows and signed macOS executables into the temporary site
artifact used by the web configurator.
The configurator offers Linux downloads as either a `.tar.gz` whose executable
entry has mode `0755`, or as a raw binary. Browsers cannot assign POSIX execute
permissions to a direct download, so extracting the recommended archive
produces a binary that can be run immediately without `chmod`; the raw option
may require `chmod +x`. Windows downloads remain direct `.exe` files. macOS
downloads are archives so the signed executable remains alongside `.c2t.env`.
The release is ad-hoc signed for executable integrity, not notarized with an
Apple Developer ID; on first launch macOS may therefore require confirmation
in Privacy & Security.
Release packages contain an empty configuration area; provision a downloaded
executable in the browser or locally with `tools/embed_config.py` when needed.
Never put Telegram credentials in a GitHub release.

## License

This project is licensed under the **GNU General Public License v3.0** (or later) - see the [LICENSE](LICENSE) file for details.

Copyright (C) 2026 Antonio Ricciardi

