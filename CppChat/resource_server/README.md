# CppChat ResourceServer

This public checkout contains source/resources only. Local CMake project files
and build directories are not published; create your own build configuration
for these source files plus `ChatServerShared/BusinessExecutor.cpp`. Build commands
below document the development setup, not a bundled build system.

## Source boundaries (2026-10-06)

- `main.cpp` calls the application entry only.
- `ResourceConfig.h/.cpp` owns environment settings, defaults and bounds.
- `ResourceProtocol.h` owns this connection's existing IDs, size limits and error envelope.
- `ResourceStore.h/.cpp` owns token/TTL checks, file access, metadata, chunks, resume and file locks; it does not own sockets.
- `ResourceServerApp.h/.cpp` owns async sessions/listener, bounded worker dispatch and process startup/shutdown.

This is an implementation reorganization, not a new resource protocol or shutdown guarantee.
Existing environment keys, chunk offsets, authorization and limits are unchanged.
Resource packets are not part of the chat connection's negotiated `request_id` contract.

## Backend credential update (2026-10-03)

Coordinate this source with the updated StatusServer and both chat-node projects.
Resource authentication now checks the active Token and its positive Redis TTL
atomically. Legacy non-expiring Tokens are rejected; reconnect by logging in again.
The chat node renews the one-hour active session lease while the client remains
online. ResourceServer Redis connect/command timeouts are two seconds.
If using a Redis test double, support the authentication EVAL (TTL plus GET), not
just a plain GET. x64 Debug/Release builds and isolated TCP/fixture tests have
passed; this is not a real Redis/MySQL end-to-end proof.

See `docs/backend-architecture.md` and the backend `ChatServerShared/README.md`
for coordinated upgrade requirements.

This service keeps chat images outside the 2 KB chat messages. It uses a separate TCP
connection and the same big-endian `uint16 id + uint16 body length + JSON` framing as
the chat connection. Every request includes the logged-in `uid` and `token`. Uploads
are written in 32 KB chunks and can resume from the acknowledged byte offset.

Build with the existing vcpkg installation (Boost, jsoncpp, hiredis, OpenSSL):

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=E:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build --config Release
```

The executable defaults to `127.0.0.1:8093` and Redis at `127.0.0.1:6380`.
The shared BusinessExecutor is compiled from the sibling backend directory.
Override `-DCPPCHAT_SHARED_SERVER_DIR=<path>` when packaging this service separately.
Network reads/writes are asynchronous; idle connections do not reserve a worker.
File/Redis work uses bounded FIFO worker lanes. Same-file chunks and metadata
are serialized by file locks, while different files may run concurrently.
`CPPCHAT_RESOURCE_WORKERS` defaults to 4 (1-16), `CPPCHAT_RESOURCE_QUEUE_LIMIT`
to 256, `CPPCHAT_RESOURCE_MAX_CONNECTIONS` to 128, and
`CPPCHAT_RESOURCE_TIMEOUT_SECONDS` to 30. Excess connections close; full work
queues return an explicit busy response. Idle/incomplete I/O times out; an
operation already running is not forcibly cancelled or rolled back on timeout.
Set `CPPCHAT_RESOURCE_LISTEN`, `CPPCHAT_RESOURCE_PORT`, `CPPCHAT_RESOURCE_DIR`,
`CPPCHAT_REDIS_HOST`, `CPPCHAT_REDIS_PORT`, and, when needed,
`CPPCHAT_REDIS_PASSWORD` before starting it. The client uses the matching
`[ResourceServer]` section in `config.ini`.

For this workspace, start `start.ps1` from PowerShell. It reads the Redis
Host/Port/Passwd from ChatServer's Debug `config.ini`, passes them only to the
ResourceServer process, and starts the logging-enabled executable without
placing the password on the command line:

```powershell
powershell -ExecutionPolicy Bypass -File E:\Project\Qtemp\myPractice\CppChat\resource_server\start.ps1
```

Use `-ChatServerConfig` or `-Executable` to override the defaults. A new client
login refreshes its Redis token; it does not require restarting ResourceServer.
Restart ResourceServer when its executable or environment settings change;
logging in again alone does not require restarting it.

For an existing database, run
`E:\Project\VStemp\GateServer\GateServer\sql\season2_image_migration.sql` once.
Then start Redis/MySQL, the existing chat services, and this ResourceServer. Set
`CPPCHAT_REDIS_PASSWORD` to the same Redis password used by the chat services;
do not commit that password to this repository. Keep the resource service on a
trusted network because it uses the existing chat login token.

In the client, choose an image with the file button. The upload finishes before
the chat message is sent. On receiving an image or loading history, the client
downloads it asynchronously. Re-selecting the same image for the same recipient
uses the same file ID and resumes from the server's stored offset. This does not
yet include an explicit pause/resume control or non-image file attachments.
An interrupted download keeps its in-memory partial data; click the failed image
to retry from that offset. The partial data is not persisted across app restarts.

Keep `CPPCHAT_RESOURCE_DIR` private. Only the uploader and the intended recipient
may download an image. Both must present the current chat login token.
The default `resource_data` path is relative to the server's working directory;
set an absolute `CPPCHAT_RESOURCE_DIR` outside the source checkout for real data.
`resource_data` holds uploaded image bytes and metadata, not logs. `build-logging`
is a separate CMake build directory created to avoid overwriting a running
executable; it is not runtime data.

Logging follows the existing ChatServer convention: `LOG_DEBUG/INFO/WARNING/ERROR`
include timestamp, service name, level and thread ID, and are written to
`logs/ResourceServer_YYYY-MM-DD.log` under the server's working directory as well
as the console. Upload/download start, completion and failure are logged without
tokens, passwords, local image paths or image bytes. Normal TCP EOF is DEBUG,
not an error. Rebuild and restart the service after source changes; the already
running executable will continue using its previous logging behavior.

## Image composer and regression validation (2026-09-30)

The client accepts screenshot images from the clipboard, pasted local image
files, dragged image files, and images selected with the file button. Selection
inserts a preview into the composer; click Send or press Enter to upload it.
Preview scaling does not change the original uploaded image bytes. Text and
images are sent in document order, and image drafts survive switching contacts.
If an upload fails, that image and the remaining unsent content return to the
draft in their original order so they can be sent again.

ResourceServer must be running with the same Redis settings as ChatServer.
The login response token is checked against `resource_token_<uid>`; an
`invalid token` response must be resolved by checking the login session and
service configuration. Authentication is not bypassed for clipboard images.

Client and backend test scripts and temporary validation builds were removed at
the user's request on 2026-10-06. Historical isolated checks used temporary data
and a RESP/TTL fixture, not the user's Redis database. Qt clipboard/composer/draft
behavior and real deployments still require separate regression.

Image messages are displayed without the text-message bubble or its arrow.
The preview keeps the image aspect ratio and uses the screen's pixel density;
clicking it opens a viewer that draws the original image and offers fit, zoom,
and original-size controls. The chat TCP packet still uses request 1017 with
`msg_type=1` and a file ID. This is why ChatServer still logs a chat message:
ResourceServer carries the image bytes while ChatServer carries its message
metadata. Client logs explicitly label the queued message as `type=image` or
`type=text`, without dumping image bytes or message content.

## Performance integration (2026-10-04)

The async sessions and shared BusinessExecutor are retained. Each business worker
now reuses its own hiredis context; every request still checks the current token
and positive TTL. A failed context is recreated on its next use, with AUTH and
the existing two-second timeouts. UploadInit advertises `upload_window=4`, while
chunk size stays 32 KiB. Old clients remain valid; clients talking to an older
server fall back to one chunk per acknowledgement.

Failed checksums remove the invalid partial upload. A full partial file left
before rename is verified and recovered on UploadInit. Isolated tests cover
these paths plus token rotation, access control, resume, Redis reconnect,
concurrent files, connection caps and idle timeouts. These are historical results;
the corresponding test programs and scripts are no longer in the checkout.
See `docs/performance-improvements.md` for results and test boundaries, and the
root README for the verified Qt/MinGW/VS/CMake dependency versions.

## 2026-10-06 Client validation cleanup

The client `scripts/` and `tests/` directories were removed at the user's request.
The `resource_protocol.py` and `performance_tests` commands above are historical validation records, not runnable entries in the current client checkout.
The later cleanup also removes `ChatServerShared/tests`, the architecture check
script and temporary validation builds. ResourceServer source, its normal CMake
configuration, and `start.ps1` remain available.
