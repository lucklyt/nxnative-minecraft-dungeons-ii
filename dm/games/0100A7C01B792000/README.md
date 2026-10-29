# Minecraft Dungeons II (`0100A7C01B792000`)

Minecraft Dungeons II, Unreal Engine 5.6.1, Nintendo SDK 20.5.15, update 1.1.3.0.

Status: experimental port. The shared memory, SSL initialization and frame-history
fixes reach the game on Mac and the menus on iPhone 16 Pro. Menu runs measured
60 fps on both platforms; sustained combat performance and complete iPhone
rendering remain unverified. The Mac Microsoft-only diagnostic reached gameplay
and completed two original cloud-save uploads after the buffered TLS write fix.
The current profile now supplies signed, build-gated Microsoft token-provider
adapters. A fresh standalone Mac process restored the genuine Microsoft account
and its original save upload returned HTTP 201 after gameplay and normal exit.
The iPhone build succeeded, but installation was blocked by device disconnection;
iPhone login and synchronization remain unverified. See the validation and open
issues below.

## Reproducing this build

Use this repository revision and your own game files and `prod.keys`. Follow the
root [build prerequisites](../../../README.md) first, then run from the repository
root (replace the example paths):

```sh
python3 scripts/build_ipa_from_nsp.py "/path/to/game-folder" -k "/path/to/prod.keys" --platform mac-app
python3 scripts/build_ipa_from_nsp.py "/path/to/game-folder" -k "/path/to/prod.keys" --keep-extracted --prepare-xcode
```

The first command builds a Mac app. The second prepares an iPhone Xcode project;
select your own development team and device in Xcode. No game files, keys,
signing identities or account credentials are included in the repository.
The profile supplies the memory settings and requests 60 fps automatically.

Networking is opt-in: add `DM_NETWORK=1` to the installed iPhone app's
`Documents/env.txt`, or the Mac app's
`~/Library/Application Support/nxnative/0100A7C01B792000/env.txt`, one setting per
line, and restart. This enables native transport and the HTTPS web applet;
for this exact game build it also selects the genuine Microsoft-only login flow
and audience-token adapters. It does not supply Nintendo credentials. Default
builds remain offline. Multiplayer is unverified.

The tested game version is **1.1.3.0**. Fixed-address patches and host hooks are
gated by module build IDs and checked against the expected instructions. For a
new game update, reuse the shared host fixes and build pipeline, then relocate
and validate the version-specific allocator, frame-rate and link-eligibility
sites before registering its builds. This profile does not opt into automatic
function-matched cuts; a successful package build for another version does not
establish that these adaptations apply or that the game boots.

## What it needs

| Setting | Where | Why |
|---|---|---|
| FMallocBinned3 constructor cut: `main+0x143b0` `bl 0xb0560` → `nop` | [`patches.py`](patches.py), `main` `c6b32f4d…` | the constructor reserves 38 GB of virtual address space for its pools; the host runs Unreal's heap on a host malloc zone (`hle_uemalloc.c` layout `UE5_6`); see [Memory](#memory) |
| `game_args -SwitchVirtualAddressSpaceSize=4064` | `game.tsv` | caps the Unreal engine virtual address region to 4064 MB to fit iPhone guest virtual memory space |
| Node area cut 1022 MB → 62 MB (`sdk+0x145cbc` / `+0x145d28`) | `dm/tools/sdk_patches.py`, `sdk` build `19003051…` | AddressRegionManager constructor: shrink node area so the capped engine region fits in the alias region |
| 5 GB guest memory block: `mem_reserve 0x140000000`, `mem_alias 0x118000000`, `mem_stack 0x10000000`, `app_pool 0xCD500000` | `game.tsv` | 5 GB reservation, 4.375 GB alias region, 256 MB stack, 3285 MB application pool, as supported on iOS without extended-virtual-addressing entitlement |
| `sys_resource 0x1400000` | `game.tsv` | the NPDM declares 20 MB of system resource |

## Memory

See [Unreal's FMallocBinned3 replaced by the host allocator](../../../docs/reference/memory.md#unreals-fmallocbinned3-replaced-by-the-host-allocator) for the shared mechanism.

- **Unreal Engine 5.6.1 FMallocBinned3 Replacement**: `nninitStartup` constructs an FMallocBinned3 in the BSS at `main+0x997e228`. The original constructor call at `main+0x143b0` (`bl 0xb0560`) would reserve ~38 GB of address space. The rewriter cuts this call to `nop` via `patches.py`, and `hle_uemalloc.c` (slot layout `UE5_6`, 32 slots) installs the host allocator on a dedicated malloc zone before any guest code runs.
- **Address Space Sizing**: With `game_args -SwitchVirtualAddressSpaceSize=4064` and the SDK's AddressRegionManager node-area cut (1022 MB → 62 MB), the guest reservation is capped to 5 GB (`mem_reserve 0x140000000`, `mem_alias 0x118000000`). Both the Mac and iPhone 16 Pro reach `nnMain` and initialize the heap; sustained iPhone gameplay remains unverified.

## Frame rate

The profile requests 60 fps. Build-gated `hle_fpslock.c` entries update both
game/render-thread copies of Unreal's `t.MaxFPS` and `rhi.SyncInterval`.
The float cvar is registered at `main+0x33eac48` and its value pointer stored
at `main+0xae27510`; the integer cvar is registered at `main+0x147dc20` and
its value pointer stored at `main+0xac96a20`. The latter changes the game's
requested present interval from two 60 Hz refreshes to one. This raises the
engine limit rather than changing the display's advertised refresh rate.

Static validation: `main+0x33fcadc` reads the `t.MaxFPS` value pointer and
`+0x33fcaf0` / `+0x33fcb0c` test the active float copy before returning it as
the engine's maximum tick rate. `main+0x147e650` and `+0x147eeec` read
`rhi.SyncInterval` through its integer value pointer. Registration and read
sites match the same two-copy UE 5.6.1 layout used by FINAL FANTASY RESONANCE.
No fixed simulation delta or frame-step instruction is patched.

Mac runtime validation with the isolated FPS host reached the animated title,
accessibility, brightness and service-terms screens. The title's 30/60 A/B
measured 30.0/60.0 fps; the 60 fps menu run reached frame 1800, including a
300-frame window with zero dropped frames and 16.67 ms on-screen p99. Other
menu windows had occasional drops. Runtime `t.MaxFPS` was already zero
(unlimited); `rhi.SyncInterval` changed from 2 to 1 on both threads. The Microsoft terms were accepted with explicit user authorization, and the
offline character menu and first coastal mission were reached. A mission
screenshot shows the HUD and terrain; movement, combat speed and iPhone
performance remain unverified.

Known presentation limitation: if the Mac window loses presentation long enough
for all eight frame-history entries to be dropped, Unreal's viewport thread
(`main+0x142b0b0`) ignores them and its last swapchain flip fence can remain
pending even after the GPU and host present queues have drained. This produces
a render-thread timeout; it was reproduced independently of the cvar addresses.
Keep the test window visible and active during menu validation. Dropped frames
must not be relabeled as displayed to release that fence.

## Xbox initialization

The first `HCInitialize`, from Xal at `main+0x7063cb4`, has platform arguments
but failed with `0x80004005`: `nn::ssl::Context::Create` was unconditionally
failed by the offline host. No HTTP singleton was created. `XblInitialize`
later called `HCInitialize(null)` and failed with `0x80070057`, causing the
fatal path at `main+0x4238d40`.

`hle_net.c` now implements the local SSL context ID and lifecycle while keeping
network availability, connection creation and handshakes offline. This lets
the library finish global initialization without faking sign-in or credentials.
The Mac boot no longer reaches that abort. See the general
[SSL context lifecycle](../../../docs/reference/services.md#ssl-context-lifecycle-while-offline).

## Network validation

Current development mode `DM_NETWORK=1` enables genuine host sockets, DNS,
verified TLS 1.2 and a native HTTPS web applet. The game still runs its own
HTTP/Xbox libraries and handles its own login responses. No Nintendo account
credentials, console device certificates or Microsoft tokens are fabricated.
See [native network and web services](../../../docs/reference/services.md#optional-native-sockets-tls-and-login-web-applet).

Native transport tests pass TCP/UDP, IPv4/IPv6 loopback, DNS, nonblocking I/O,
readiness translation and descriptor ownership. TLS integration rejects an
untrusted certificate and wrong hostname, then passes a trusted local HTTPS
handshake/read/write/peek. A native request to `https://login.live.com/` returned
HTTP 200 using normal system trust. Web applet argument/return TLVs and callback
origin/path checks pass CPU tests; the UI source compiles for Mac and iPhone.

On the reinstalled iPhone build, network mode and both 60 fps cvar copies
are active. Menu windows at frames 900–2400 measured 60.0 fps; frame 2700
measured 58.8 fps. This is menu performance, not combat validation. The earlier
iPhone build, before the link-eligibility adaptation, still showed offline play
and a disabled Microsoft link action.
The first observed NSD failure was a literal Microsoft telemetry hostname,
`self.events.data.microsoft.com`, not an OAuth login response. The local NSD
implementation now preserves literal DNS names in online mode; unsupported
environment substitutions still fail. This change alone does not prove sign-in.

The disabled-link explanation is selected at `main+0x535fe90` / `+0x535feb8`
from the `MicrosoftAccount_DisabledLocalUser` text key. It checks the account
provider through `+0x4766644`, `+0x4768abc`, `+0x428093c` and the Nintendo
identity login-status method `+0x4255cd0`. An existing user whose ID has a zero
network-service account ID (`ID+0x28`) receives status 1; a nonzero ID receives
status 2. The host's `IsNetworkServiceAccountAvailable=false` produces the zero
ID at `+0x4250850` / `+0x4250878`. A valid local UID alone therefore does not
enable the Microsoft Link action. These are static branch observations plus an
observed runtime availability query, not proof that inserting an arbitrary
network-service ID would authenticate an account. The downstream real identity
and token requirements still need validation.

Native `EventFd` is required by the actual game's wakeup path
(`main+0x106e7d4`, initial 0, flags 4). The backend implements a genuine 64-bit
counter with nonblocking/semaphore modes, blocking wakeups and read/write
readiness. Poll/Select retain counter metadata across close and guest-FD reuse.
The SDK keeps its resolver-option helpers: its observed key `0x2711` generates
a local random request ID. Both operations formerly failed unconditionally.

The libcurl C SSL path now shares the real TLS backend. Live debugging found
`nnsslContextGetContextId` returning failure for a constructed zero-ID context,
which stopped libcurl before context creation. SDK 20.5.15 disassembly confirms
that both ID getters return the local field successfully, including zero. This
is corrected and covered by the C ABI HTTPS test. Game-driven TLS reached
successful certificate verification, then the old
certificate-output wrapper incorrectly rejected its absent optional buffer.
That wrapper now preserves success with no exported certificates and accepts
optional size/count outputs, following the original SDK's overload. Actual
authorization was subsequently tested as described below.

The two exact-build UI eligibility calls at `main+0x535fe90` and
`main+0x5633720` now target the existing NIFM availability import at
`main+0x7388790`. This is a platform-limit adaptation for local host users with
no Nintendo NSAID. Default offline mode keeps the action disabled. The unlinked
account and controller checks and actual XAL authentication are preserved.
The isolated Mac test reached `XalAddUserWithUiAsync` and observed synchronous
HRESULT zero, then displayed the real Microsoft RemoteConnect dialog. Its
`microsoft.com/link` URL and eight-character device code populated asynchronously
from the server after the initial empty dialog. The user completed authorization
at Microsoft's official device-login page. Hardware-breakpoint observations then
confirmed `XalAddUserWithUiResult` success (`main+0x42436e0`, HRESULT zero), both
service-token Result/ResultSize calls succeeding (`+0x42450e4`, `+0x42451c8`),
and the final Microsoft identity result (`+0x4243b10`) with HRESULT zero and a
non-null user. No credentials, token values or user identifiers were logged.

**Historical Switch client blocker:** the next stage requests the primary platform's native auth
proof. Runtime dispatch at `+0x4280ba8` selects `+0x5cb76c0`, whose identity
method at `+0x5cb7780` is the Nintendo provider `+0x42565e0` with local user
index zero. Its automatic-login request is accepted (`+0x4255278`), but the
native-auth completion (`+0x4767c68`) returns false and an empty FString (count
zero). The game's application error at `+0x5cb74d4` is code 29, message
"Account linking failed" in the selected language. The Nintendo backend link
helper at `+0x476a6a0` has not been observed; its static endpoint is
`/account/nintendo/link`. Microsoft success therefore does not complete game
account linking. Native Nintendo account/token support remains absent; no
substitute Nintendo IDs, proof strings or success callbacks are supplied.

An isolated command-line configuration experiment used
`-ini:Engine:[OnlineSubsystem]:DefaultPlatformService=Microsoft,NativePlatformService=Microsoft`
alongside the required address-space argument. Runtime dispatch still selected
the Nintendo native-auth path, so this did not establish an alternative primary
Microsoft mode and was reverted. The Microsoft credential cache survived the
restart: another user-code entry was not required, and XAL's result succeeded
again. This is credential-cache persistence, not successful game account restore.
A subsequent isolated Mac probe selected the shipped GDK primary flow and
preferred Microsoft provider kind zero for both native service/context lookup.
The flow was constructed, but application error 21 followed. A hardware trace at
`+0x52a4170` recorded failure enum 997 with `+0x52a56c0` and `+0x4243ec0`
on the stack. The latter is the failure dispatch after `XalTryAddUserByIdResult`
(original diagnostic at `+0x7914c90`), before cloud character listing. The exact
HRESULT and restoration failure cause remain unknown. All seven experimental
instructions were reverted; they are not profile cuts. Static analysis identifies
separate platform and PlayFab tokens in the production `/login/playfab` request
(`+0x52a6838`), but a bounded search has not located a compiled Xbox-to-PlayFab
login request. The public PlayFab `LoginWithXbox` API is a possible implementation
route, not proof that this title accepts it or restores the same cloud character.
This does not establish that Microsoft cloud storage universally requires a
Nintendo account.

Further isolated probes used the genuine interactive XAL async/result pair,
then the actual ExternalUi login state rather than silent user restoration.
Neither established a game session during observation; both were reverted.
A feasibility probe is prepared to submit a successfully acquired PlayFab-audience
Xbox token to the official `Client/LoginWithXbox` endpoint using the game's own
public title selector and `CreateAccount=false`. It records only status/error and
session-presence booleans. Fresh Microsoft authorization was requested, and no
Xbox-to-PlayFab request has yet been observed. This pending probe is not a shipped
adapter or evidence of game/cloud login success.

Automatic offline-hero cloud synchronization is disabled for this isolated local
test character before further account-provider experiments. No cloud upload or
download was performed.

**Microsoft-only service validation (2026-10-28):** after renewed official
Microsoft authorization, a diagnostic reused the game's genuine Xbox credentials
and original production title selection. Official PlayFab `Client/LoginWithXbox`
returned HTTP 200 and a real existing-account session (`CreateAccount=false`).
The production Minecraft `/login/playfab` endpoint then returned HTTP 200 with
an access token. A verified **GET** of `/spicewood/characters` returned HTTP 200
and one cloud character. No Nintendo proof was supplied; this directly establishes
that this account can authenticate and list a cloud character via Microsoft alone.
These were diagnostic requests, not the game's normal UI flow or a shipped iOS
adapter. Character detail/download/upload/create/update/delete were not requested.

The live Microsoft cached account stores the Minecraft-audience token at `+0x88`
and PlayFab token at `+0x98`; the factory swaps arguments before constructing it.
Using the other audience gives PlayFab error 1188 (`InvalidXboxLiveToken`).
Minecraft login also needs its original `X-Client-Version` (observed `1.1.3.0`),
`X-Client-Build-Number`, and `X-Client-Protocol-Version` (`11`) headers. Missing
the version header produced HTTP 400 request validation, not a license denial.
No credentials, account identifiers or character payloads were logged or saved.

**Isolated client validation (2026-10-29):** the natural-start Mac prototype
completed genuine XAL, PlayFab and Minecraft login through the original game
response callbacks, then issued and completed the original cloud-list request.
A restart also restored the Microsoft credential cache without another device
code. Two audience-token providers still rely on LLDB instrumentation; this is
not a standalone Mac/iOS adapter.

At the user's explicit request, offline-hero cloud saves were enabled and the
existing level-100, power-150 local hero entered camp, performed a few actions,
and used Save and Return to Main Menu. The setting survived a restart. Passive
observation initially recorded HTTP status zero and a retry. This was resolved
by the shared [buffered TLS write fix](../../../docs/reference/services.md#large-tls-uploads-stall-after-successful-login).
A comparison sent the original 625,463-byte save body and genuine SAS headers
to the original save endpoint and received HTTP 201. After rebuilding the host,
the game's own upload returned HTTP 201, followed by its successful completion
callback (status 200). A second test moved/attacked with the same level-100 hero
and selected Save and Return to Main Menu; that original upload again returned
HTTP 201 and successful game completion. No external uploader performed either
of these two rebuilt-game saves. The cloud list contains an OFFLINE character,
so an empty Online Heroes filter does not imply failed offline-hero cloud saves.
The list summary can remain unchanged while the separate save blob is updated.
No other hero was intentionally played or selected for these tests.

**Resolved on standalone Mac (2026-10-29):** the profile now emits signed login
adapters through `STUBS`, without a debugger or runtime code generation. The
network-enabled primary selector uses the shipped GDK factory and real XAL
interactive async/result pair. Its two audience providers locate the actual
Microsoft account map, check sparse allocation bits, object vtables and local
user index, and forward the existing PlayFab (`+0x98`) or Minecraft (`+0x88`)
FString to the original callback. Missing users or empty tokens use the original
provider and its error path. No authentication results or credentials are forged.
The original offline primary-factory selection is preserved when networking is
disabled.

A fresh Mac launch, with no login debugger attached, restored the cached account
and displayed it as linked in Settings. With the existing level-100 hero, cloud
saves enabled, camp movement and normal Save and Return to Main Menu, the
game's original upload returned HTTP 201 and its completion reported status 200.
A passive debugger was attached only after login to observe that original upload;
it supplied no tokens and changed no response. Nine synthetic ARM64 execution
tests cover audience selection, missing credentials, local-user separation,
sparse holes and online/offline factory selection (`test_minecraft_primary.py`,
optional `unicorn` package; no game files, credentials or network required).

**Open:** verify fresh authorization, credential expiry/recovery, cloud character
selection and synchronization on iPhone, and multiplayer. The signed iPhone build
with these adapters succeeded, but installation failed when the connected device
became unavailable. Nintendo proof is still absent; the network-enabled profile
uses genuine Microsoft credentials instead.
Offline-hero cloud synchronization is now enabled for the user-requested level-100
upload test. Preserve the separate level-one local test hero and existing cloud data.

## Startup wait after the movies

On both platforms the movies finish and the controller setup completes, then
presentation stops. A Mac capture shows the Xbox Game Studios logo correctly.
The Mac reaches about 80–95 presents before the wait; the exact frame varies.
The GameThread waits through `main+0x1501228`, `+0x150194c`, `+0x14fdc90`
(`FlushRenderingCommands`), under viewport resizing at `+0x1efc664`.
RenderThread and RHIThread are idle on their task events. All submitted GPU
fences and presents are complete, and there are no pending uploads. Restoring
the native allocator and 64 GB alias region reproduced the identical wait,
excluding the allocator replacement as its cause.

**Resolved on the Mac:** the incomplete fence uses `ESyncDepth::Swapchain`.
Implementing [display frame history](../../../docs/reference/services.md#display-frame-history-for-swapchain-fences)
with the SDK's capacity query and timestamp units allows the flip tracker to
signal it. The game reaches its animated title and responds to A by opening
the Chinese accessibility menu. No wait is skipped. The phone build with this
fix was installed and launched, reaching 600 frames at about 30 fps without
the previous abort or startup wait. Gameplay is being tested by the user.

**Open:** the phone reports dynamic texture-handle reflection failures for
vertex shaders `d1c8e612ec7462b6`, `210d8abb3b9996c5` and
`cf8fc9f44d647a4e` with `tex_dyn_slots off`. Rendering beyond the menus needs
inspection before declaring the port playable.

Validation so far: host unit tests, the SSL lifecycle test with ASan/UBSan,
the frame-history lifecycle test with ASan/UBSan, documentation checks, and the static regression harness over the locally
available game (15,826 shaders, zero newly failing or changed shader outputs).
These checks do not establish playability.
