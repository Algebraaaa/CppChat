# CppChat ResourceServer

## Voice messages (2026-10-19, 0.1.16)

Upload init `media_type=3` selects stored audio (default 1 remains image). The kind,
sender, recipient/group, size and checksum are immutable for a file ID. Audio lives
under `audio/`, inaccessible to old resource binaries after rollback. Ready records
carry an `audio:` prefix so images cannot be relabeled as audio or vice versa.
Only canonical RIFF/WAVE PCM, 16000 Hz, mono, 16-bit little endian, 1–60 seconds,
is accepted; the service checks the final header and exact length before marking ready.
The maximum canonical file is 1,920,044 bytes, below the 2 MiB limit.
Group audio uses the same live membership RPC for every protected request.
Chat type 3 is new; 0/1/2 retain their meanings. Login negotiates `voice_messages`;
old sessions receive a text upgrade notice, while durable history retains real audio.
Media metadata includes format, byte size and duration derived from the validated PCM
length, so production MySQL needs no new columns or tables.
Client SQLite upgrades idempotently to v4 with a pending-audio index. Threads holding
old upgrade notices restart history synchronization once, retaining local deletion
tombstones; old clients refuse the newer database rather than misreading audio as text.

Reuse: the installed Qt 6.12.0 [QAudioSource](https://doc.qt.io/qt-6/qaudiosource.html)
and [QAudioSink](https://doc.qt.io/qt-6/qaudiosink.html), existing transport/SQLite and
the [RIFF format](https://learn.microsoft.com/en-us/windows/win32/xaudio2/resource-interchange-file-format--riff-).
Reviewed Qt's [BSD-3-Clause recorder example](https://github.com/qt/qtmultimedia/blob/v6.12.0/examples/multimedia/audiorecorder/audiorecorder.cpp)
for device/error lifecycle; no example source was copied or codec library added.
The small shared canonical container validator avoids a general media parser and
backend-dependent WAV encoder settings. It does not implement an audio codec.
Capture is explicit and bounded, uses Qt's GUI-thread QIODevice interface, and
rejects devices that cannot supply the required format. Real-time calls preempt
stored recording/playback. Real device/permission, speaker and weak-network
acceptance remains separate from synthetic PCM and loopback/cloud transport tests.

## Group images (2026-10-19, 0.1.15)

An upload may specify `recipient=0, thread_id=<actual group>`; its metadata and ready
record are bound to that group. Existing private uploads retain their original format.
Group files live in a separate `groups/` namespace, so rolling back to a previous
resource binary cannot treat a group upload as an uploader-owned private resource.
Every group upload init, chunk/resume and download checks current membership through
`MediaAuthorization.AuthorizeGroup`. The uploader has no exemption after leaving.
Removing a member or disbanding denies subsequent requests; bytes already downloaded
to a device cannot be revoked. No MySQL table changes are needed.

The authority is a separate listener hard-bound to `127.0.0.1`, using the existing
chat-node token and transactional `MysqlMgr::GetGroup` policy. Its default port is
`SelfServer.RPCPort + 5`, overridden by `[MediaAuthorization] Port` if necessary.
ResourceServer reads up to two ports from `CPPCHAT_MEDIA_AUTH_PORTS` (default
`50060,50061`), always using loopback. Each RPC has a one-second deadline; unavailable
nodes may fall back, but a membership denial is final. All authorities unavailable
means denial, never trust in file ownership or cached membership. Request/response
sizes, concurrency and polling threads are bounded; compression is disabled.

Reuse: existing [gRPC C++ services](https://grpc.io/docs/languages/cpp/basics/) and
[deadlines](https://grpc.io/docs/guides/deadlines/), with the existing Apache-2.0
gRPC/Protobuf toolchain; no upstream implementation was copied. The
[Matrix authenticated-media proposal](https://github.com/matrix-org/matrix-spec-proposals/blob/main/proposals/3916-authentication-for-media.md)
informed endpoint authentication, not this project's group policy. Reusing the
current DAO and transport avoids importing another application's storage/protocol stack.

Dependency review: local vcpkg gRPC is 1.81.1. The upstream
[HTTP/2 memory exhaustion advisory](https://github.com/grpc/grpc/security/advisories/GHSA-hf3w-6hpw-qp67)
includes this version; patched releases are 1.82.2/1.83.1. This change does not claim
to patch gRPC itself. The new endpoint must remain trusted local IPC, with no public
proxy or forwarded port. Public resource packets cannot select an RPC destination or
provide raw gRPC traffic. An overall dependency upgrade remains separate work.

Tests: real SQL/Redis and two chat nodes cover loopback binding, scoped ready records,
cross-node notification/history, resume, duplicate sends, private/group scope refusal,
nonmembers, removal/leave/disband, authority failover and fail-closed behavior.
Qt tests cover the capability gate, target-scoped upload and picture rendering.

ResourceServer is a backend module at the repository root, alongside GateServer,
StatusServer and the chat nodes. Its `CMakeLists.txt` reuses the shared helpers in
`cmake/` and is included by the root server build. Configure from the repository
root; the Qt client accesses this service over the network.

The directory split follows the sibling server modules in this project and
[TrinityCore's server layout](https://github.com/TrinityCore/TrinityCore/blob/master/src/server/CMakeLists.txt).
Only the directory organization and build references are adapted; no upstream
source code, dependencies or license obligations are added by this move.

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

See [backend architecture](../CppChat/docs/backend-architecture.md) and the
[shared backend notes](../ChatServerShared/README.md) for coordinated upgrade requirements.

This service keeps chat images outside the 2 KB chat messages. It uses a separate TCP
connection and the same big-endian `uint16 id + uint16 body length + JSON` framing as
the chat connection. Every request includes the logged-in `uid` and `token`. Uploads
are written in 32 KB chunks and can resume from the acknowledged byte offset.

Build from the repository root with the existing vcpkg installation (Boost, jsoncpp, hiredis, OpenSSL):

```powershell
.\CppChat.cmd build -Target ResourceServer -Configuration Release
```

The executable defaults to `127.0.0.1:8093` and Redis at `127.0.0.1:6380`.
The shared BusinessExecutor is compiled from `ChatServerShared` using the root
server build. The executable and runtime data paths remain under `build/server`
and `.runtime/ResourceServer`, respectively.
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

For this workspace, use the repository's unified launcher. It supplies the
ResourceServer Redis settings and data directory without putting the password
on the command line:

```powershell
.\CppChat.cmd start -NoClient
```

A new client login refreshes its Redis token; it does not require restarting ResourceServer.
Restart ResourceServer when its executable or environment settings change;
logging in again alone does not require restarting it.

For a new installation, import [the complete schema](../database/mysql/schema.sql)
to create all project tables, including image metadata (see
[database notes](../database/README.md)). Importing this schema does not upgrade
existing tables.
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
See [performance history](../CppChat/docs/performance-improvements.md) for results and test boundaries, and the
root README for the verified Qt/MinGW/VS/CMake dependency versions.

## 2026-10-06 Client validation cleanup

The client `scripts/` and `tests/` directories were removed at the user's request.
The `resource_protocol.py` and `performance_tests` commands above are historical validation records, not runnable entries in the current client checkout.
The later cleanup also removes `ChatServerShared/tests`, the architecture check
script and temporary validation builds. ResourceServer is built and started
through the repository's CMake project and unified `CppChat.cmd` entrypoint.
