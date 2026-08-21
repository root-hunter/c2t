# c2t

`c2t` listens for clipboard changes and can deliver supported clipboard data
to a Telegram chat.

Use the [client-side configurator](https://root-hunter.github.io/c2t/) to
download the latest release and embed its Telegram settings directly in the
browser. The page has no backend, analytics or persistent storage: values stay
in browser memory and are written only to the downloaded executable.

## Post-compilation configuration

Every Linux ELF and Windows PE build contains a reserved `.c2tcfg` section.
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
`TELEGRAM_SEND_WINDOW_INFO`, `TELEGRAM_MAX_FILE_BYTES`, `C2T_VERBOSE`,
`C2T_QUEUE_MAX_BYTES`, `C2T_QUEUE_MAX_ITEMS`, `C2T_DELIVERY_ATTEMPTS`, and
`C2T_RETRY_DELAY_MS`. Environment variables take precedence over embedded
values, which in turn take precedence over defaults. Setting an environment
variable to an empty value explicitly masks its embedded value.

Embedded secrets are operationally convenient, but are not encrypted and can
be extracted by anyone who can read the executable. Restrict access to the
provisioned file. For Windows, provision first and apply the Authenticode
signature afterwards; patching a signed PE file invalidates its signature.

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
used as a fallback. Missing or inaccessible fields are omitted.

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

## Build and release

GitHub Actions builds and verifies Linux x86_64 and ARM64 with both musl and
glibc, plus Windows x86_64, on every push and pull request. The musl
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
packaged Windows build, generated release notes, and a `SHA256SUMS` file. Raw
executables are not attached to GitHub Releases. The Pages workflow extracts
only the portable musl and Windows executables into the temporary site
artifact used by the web configurator.
Configured Linux downloads are wrapped in a `.tar.gz` whose executable entry
has mode `0755`, because browsers cannot assign POSIX execute permissions to a
directly downloaded file. Extracting the archive produces a binary that can be
run immediately without `chmod`. Windows downloads remain direct `.exe` files.
Release packages contain an empty configuration area; provision a downloaded
executable in the browser or locally with `tools/embed_config.py` when needed.
Never put Telegram credentials in a GitHub release.
