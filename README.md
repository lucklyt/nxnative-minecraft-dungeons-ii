# nxnative — Minecraft Dungeons II adaptation

This repository is an experimental adaptation of [seedds/nxnative](https://github.com/seedds/nxnative)
focused on **Minecraft Dungeons II**, title ID `0100A7C01B792000`, game version
**1.1.3.0**, on Apple Silicon Mac and iPhone. The inherited games gallery below
is upstream project information; other games have not been revalidated against
this branch's shared host changes.

## Player features

- **60 FPS target** on iPhone and Apple Silicon Mac, enabled automatically for
  the supported game version. Actual frame rate depends on the device and scene.
- **Sign in with your own Microsoft account** from the game's Settings.
- **Microsoft cloud-save synchronization:** access your account's cloud
  characters and upload progress through the game's normal save flow, including
  cloud saving for offline heroes when enabled in the game.
- **Automatic sign-in restoration** after restarting the app, while Microsoft
  authorization remains valid. Settings shows your account name and linked state.
- **Networking enabled by default:** no configuration-file editing is needed
  after installing a newly built app.

Start with the [build and installation guide](#build-and-install).
Bring your own game files and account; no personal saves or credentials are
included in this repository.

## Current support and limitations

The supported version is **1.1.3.0**. Mac gameplay and iPhone level-100 camp
play have been tested. Microsoft sign-in, cloud-character retrieval, progress
upload and sign-in restoration after two full app restarts passed iPhone tests.
Menu tests measured 60 FPS on both platforms; sustained iPhone combat performance
and complete rendering remain unverified. A 60 FPS target does not guarantee
60 FPS in every scene.

First-time users link their account in the game's Settings. Existing users
upgrading from an older build need one successful Settings login to enable
automatic restoration. Expired/revoked authorization, some login failure paths
and account unlinking need further testing. Multiplayer is unverified.
The tested Microsoft cloud-save path does not require a Nintendo account.

This is an experimental adaptation for this game. New game updates may require
updated patches; other games in the inherited upstream gallery have not been
revalidated against this fork.

## Implementation and validation

Upstream updates through `b06f4e0e` (2026-11-14) are merged. This fork retains
Microsoft sign-in/cloud saves and the iPhone memory configuration. The updated
build still needs device testing; the phone results below describe the earlier
tested build. See the [merge validation notes](dm/games/0100A7C01B792000/README.md#upstream-synchronization-2026-11-14).

The [game-specific notes](dm/games/0100A7C01B792000/README.md) document the memory
adaptations, frame pacing, Microsoft authentication and cloud-upload fixes.
These changes are stored as source and game-profile data and apply automatically
when repacking the supported version. Shared host fixes may also affect other
games that use those paths.

Mac and signed iPhone builds passed, along with host tests, 46 ARM64 login-adapter
tests, 14 registry tests and four documentation tests. Original game uploads
returned HTTP 201 and completion status 200. The latest cross-game regression
attempt is blocked by missing locked Metal tools; it is not a cross-game
compatibility guarantee. See the game-specific notes for detailed evidence and
remaining validation gaps.

## Build and install

These steps are for **Minecraft Dungeons II 1.1.3.0**, title ID
`0100A7C01B792000`. Use your own game files, keys, Apple signing account and
Microsoft account. The repository contains no game data, account tokens or
personal saves. The tested Microsoft-only path does not require supplying a
Nintendo account. Multiplayer is not validated.

### 1. Get this fork and prepare the game

On an Apple Silicon Mac, install Xcode and Homebrew, then run:

```sh
git clone https://github.com/lucklyt/nxnative-minecraft-dungeons-ii.git
cd nxnative-minecraft-dungeons-ii
brew install xcodegen zstd spirv-cross glslang
brew install --cask dotnet-sdk
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install lz4 capstone fonttools "backports.zstd; python_version < '3.14'"
```

If you already cloned this fork, run `git pull` before rebuilding. Put the base
game, the **1.1.3.0 update** and any DLC you own in one folder outside the
repository. Keep `prod.keys` outside the repository too. The packer selects the
newest update in that folder: do not include a newer, unvalidated update.
New versions may need updated build-specific patches even if packaging succeeds.

### 2. Rebuild and install

Run from the repository root, replacing the example paths with your own:

```sh
# Prepare the iPhone project, including this fork's game adaptations.
python3 scripts/build_ipa_from_nsp.py "/path/to/game-folder" -k "/path/to/prod.keys" --keep-extracted --prepare-xcode
```

Open the generated
`/path/to/game-folder/0100A7C01B792000_build/xcode/DmIOS.xcodeproj` in Xcode:

1. Sign in with your own Apple ID in Xcode Settings → Apple Accounts.
2. Select the **DmIOS** target → **Signing & Capabilities**, enable automatic
   signing, and select your own team. Use a unique bundle identifier if needed.
3. Connect and unlock the iPhone, trust the Mac and enable Developer Mode when
   requested. Select the actual iPhone as the run destination.
4. Select the game's scheme and press **Run**. If iOS asks, trust/verify your
   developer app in Settings → General → VPN & Device Management.

See [Xcode installation details](docs/development.md#build-and-run-on-a-device-with-xcode)
for signing and memory-capability requirements. Keep the same bundle identifier
when updating, and back up your saves before replacing an existing installation.

Alternatively, omit `--keep-extracted --prepare-xcode` to produce an unsigned IPA,
then sign and install it with your own sideloading tool. For a Mac app instead:

```sh
python3 scripts/build_ipa_from_nsp.py "/path/to/game-folder" -k "/path/to/prod.keys" --platform mac-app
```

Repacking from this fork applies the supported profile automatically; no manual
binary edits, debugger patches or another person's login cache are needed.

### 3. Start with networking already enabled

New builds from this fork **enable networking by default for this game**, on
both iPhone and Mac. Open the installed app normally; no file editing, launch
variable or extra switch is needed. Microsoft sign-in and cloud saves use the
game's normal interface.

If upgrading an old installation, an existing explicit `DM_NETWORK=0` override
still keeps it offline. Remove that line from its `env.txt` (or change it to
`DM_NETWORK=1`) and fully restart the app. Old binaries must be rebuilt and
reinstalled to gain the new default; updating the README alone does not change
an installed app.

For optional offline use, set `DM_NETWORK=0` in the app's `env.txt` and restart:
on iPhone this is Files → On My iPhone → the game's folder, beside `save/` and
`log.txt`; on Mac it is
`~/Library/Application Support/nxnative/0100A7C01B792000/env.txt`.
This profile default does not enable networking for other games.

For build or runtime issues, include the app's `log.txt`, game version,
device/iOS version and visible error when reporting a problem. See the
[game-specific notes](dm/games/0100A7C01B792000/README.md) for technical details
and remaining validation gaps.

## Upstream nxnative overview

# nxnative

Play your own Nintendo Switch and Switch 2 games natively on Apple silicon Macs, iPhone and iPad. One script packs your game into a Mac app or an unsigned IPA that you sideload.

It is not an emulator: the game's own code runs directly on the Apple chip, with Switch graphics rebuilt on Metal. Nothing from any game is in this repository, and the project is not affiliated with Nintendo or any game publisher.

[Games](#games) · [Community front ends](#community-front-ends) · [Requirements](#requirements) · [How to play](#how-to-play) · [Discussions](#discussions) · [For developers](#for-developers)

## Games

The full [tested games table](dm/games/README.md#games) includes original Switch games, tested builds, engines and graphics APIs. Each game's note records its tested platforms, progress and known issues.

These Switch 2 games have passed Mac gameplay testing. Build them from a [`.nup` file or a dump folder](#switch-2-games):

- [Donkey Kong Bananza](dm/games/040074A01BF12000/README.md)
- [Fire Emblem Fortune's Weave](dm/games/04007EF01F8AE000/README.md) — up to 120 fps
- [Hyrule Warriors: Age of Imprisonment](dm/games/04003F40232DE000/README.md)
- [Kirby Air Riders](dm/games/040077201BE50000/README.md)
- [Mario Kart World](dm/games/0400C3F00006E000/README.md)
- [Mario Tennis Fever](dm/games/0400D4201C6EA000/README.md)
- [Nintendo Switch Sports Resort](dm/games/04008E001F098000/README.md)
- [Pokémon Pokopia](dm/games/0400A3A01EE4C000/README.md)
- [PRAGMATA](dm/games/0400C76024710000/README.md)
- [Splatoon Raiders](dm/games/0400380020056000/README.md)
- [Star Fox](dm/games/0400E88025B1A000/README.md)
- [The Legend of Zelda: Ocarina of Time](dm/games/0400486024DEC000/README.md)
- [Yoshi and the Mysterious Book](dm/games/0400245021D2C000/README.md)

## Community front ends

Community projects build on nxnative with tools to turn your games into apps for your iPhone, iPad or Mac without the command line, and libraries to organize and launch them. They are separate projects: questions and bugs about them go to their own pages.

- [NX2Apple](https://github.com/LNPANDY/NX2Apple) — Mac builder with a shared iPhone/iPad game library.
- [NX Play](https://github.com/varyform/nx-native-wrapper) — Mac library with device installs and save sync.
- [NxNative Launcher](https://github.com/rennan101/nxnative_Launcher) — Mac launcher with guided builds and controller mapping.
- [LumaNX](https://github.com/Xc1Ym/LumaNX) — Mac game builder with IPA export.
- [GameCase](https://github.com/Tiaoge-tou/GameCase) — iOS game library based on LiveContainer.

## Requirements

- **Mac:** Apple silicon, macOS 26.6 or later. Building games requires Xcode 27.2.
- **iPhone:** A17 Pro chip or later, with iOS 26 or later.
- **iPad:** M1 chip or later, with iPadOS 26 or later.

The games' textures are BC-compressed, which older iPhones cannot read. Switch 2 builds are tested on the Mac; check each game's note for device-specific validation.

For a game without a profile, the script writes a survey and tries a provisional build with guessed settings. That build may not start; see [Port another game](docs/development.md#port-another-game).

## How to play

Switch 2 games are built from one `.nup` file or a folder of their files, not from NSPs, and are tested on the Mac only: see [Switch 2 games](#switch-2-games).

### Build from the command line

You need a Mac with macOS 26.6 or later, [Xcode](https://developer.apple.com/download/all/?q=Xcode%2027.2) 27.2, [Homebrew](https://brew.sh) and Python 3.12 or newer (an arm64 one, not an Intel Homebrew's). An iPhone build also needs Xcode's iOS platform (Xcode → Settings → Components).

The shaders are built with Xcode 27.2's Metal compiler. Select it before step 1: `sudo xcode-select -s /Applications/Xcode.app`. With several Xcode versions installed, name this one `Xcode-27.2.app` and select it with `sudo xcode-select -s /Applications/Xcode-27.2.app`.

**1. Install the tools** (only once), in Terminal:

```
xcodebuild -downloadComponent MetalToolchain
git clone https://github.com/lucklyt/nxnative-minecraft-dungeons-ii.git && cd nxnative-minecraft-dungeons-ii
brew install xcodegen zstd glslang
python3 -m pip install lz4 "capstone>=5,<6" fonttools nsz zbic "backports.zstd; python_version < '3.14'"
```

**2. Put your game in a folder**: the game, plus its update and DLC if you have them (`.nsp`, `.xci`, `.nsz` or `.xcz`):

```
~/Games/MarioWonder/
  Super Mario Bros. Wonder [010015100B514000][v0].nsp
  Super Mario Bros. Wonder [010015100B514800][v327680].nsp
```

**3. Build**, with your game folder and your `prod.keys`:

```
python3 scripts/build_ipa_from_nsp.py ~/Games/MarioWonder -k ~/keys/prod.keys
```

The IPA appears in the game's folder. The first build takes 30–60 minutes and needs free disk space of about twice the game's size plus 10 GB; building again is much faster.

To play on the Mac instead, add `--platform mac-app`: you get a Mac app of the game to open from Finder. All options: [The packing script in detail](docs/development.md#the-packing-script-in-detail).

### Install

Install the IPA on your iPhone or iPad with a sideloading tool such as AltStore, Sideloadly or LiveContainer. If your tool offers **Increased Memory Limit**, turn it on: the bigger games need it.

Game too big to install? Build again with `--romfs separate`, and run the command the script prints once to copy the game files to your phone ([Quick reinstalls](docs/development.md#quick-reinstalls)). With AltServer, also add `--shader-files`: it cannot install a file of 2 GiB or more.

Controls, saves and settings: [Playing](docs/playing.md).

### Switch 2 games

A Switch 2 game is built from one [NUP](https://github.com/a55uka/nup) container (a `.nup` file) or a folder of its files, not from NSPs. Neither needs `prod.keys`. A game this project has no profile for is surveyed and gets a provisional profile, as NSPs do ([Port another game](docs/development.md#port-another-game)), which may not boot. You get a Mac app; iPhone and iPad are untested.

**From a `.nup` file**, which holds the base game, its updates and DLC in one file. Give the file, or a folder that holds it:

```
python3 scripts/build_ipa_from_nsp.py ~/Games/DonkeyKongBananza.nup --platform mac-app
```

[Hyrule Warriors: Age of Imprisonment](dm/games/04003F40232DE000/README.md) is played this way: its 47 GB NUP unpacks in under five minutes. A NUP that carries the game's control data (its NACP) and icon gives the app the game's name and icon; otherwise the name comes from the game's profile and the app has no icon unless you give it one with `--icon <image>` (`ICON` in Spyder; a JPEG or PNG of any size).

A NUP without the base game also works, when its base package is empty or it has none and its updates are marked `standalone_update`: the update is taken as the whole game, and the script says so. If the game then stops on missing files, that update holds only what changed: pack a NUP with the base game. The newest update is selected by default; `--package v<N>` selects another version in the container. Such a container has no `v0` (base game) to select.

**From a folder of files**. The script expects this layout (`updates` and `dlc` are optional):

```
~/Games/DonkeyKongBananza/
  base/exefs/                     main, main.npdm, sdk, rtld
  base/romfs/
  updates/458752/exefs/           the update's version, as a decimal number
  updates/458752/romfs/           the patched RomFS, not a patch
  dlc/040074a01bf13001/0/romfs/   the DLC's title ID
```

```
python3 scripts/build_ipa_from_nsp.py ~/Games/DonkeyKongBananza --platform mac-app
```

An update's folder alone will do (no `base/`), and so will `exefs/` and `romfs/` directly in the folder, as `nup extract` writes them, which count as `base/`. The app has no icon, because the folder holds none; `--icon <image>` (`ICON` in Spyder) gives it one. To turn such a folder into a NUP: `nup pack ~/Games/DonkeyKongBananza game.nup`.

## Discussions

Questions, how a game runs on your Mac or iPhone, and ideas go in [Discussions](https://github.com/seedds/nxnative/discussions); [Issues](https://github.com/seedds/nxnative/issues) are for bugs, with the log. Ocarina of Time's performance (frame rate, resolution, which Macs hold 60 fps) is followed in [Ocarina of Time: state on main](https://github.com/seedds/nxnative/discussions/1789).

## For developers

[How it works](docs/development.md) · [Game notes](dm/games/README.md#games) · [Bring-up and debugging](docs/agents/README.md) · [Front ends](docs/frontends.md) · [Contributing](CONTRIBUTING.md)

## Acknowledgements

This project is built on other people's work ([licenses](dm/third_party/README.md)):

- **[d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile)** (MIT) developed this native-execution approach, for Diablo II: Resurrected. nxnative started as a copy of its generic parts: the NSO rewriter, the host that stands in for Horizon, the NVN-on-Metal layer and the iOS app shell. Fixes go both ways.
- **Ryujinx** (MIT): its shader translator (`Ryujinx.Graphics.Shader`, `Ryujinx.Common` and `Spv.Generator` 1.3.2, in `dm/third_party/ryujinx` with a few fixes listed in [its README](dm/third_party/ryujinx/README.md)) turns the games' shaders into SPIR-V. Its emulation of the kernel and system services is also a reference when the host's behaviour is in doubt.
- **[Atmosphère](https://github.com/Atmosphere-NX/Atmosphere)**: its kernel, mesosphère, is the reference for Horizon's synchronization semantics (address arbiter, condition variables).
- **[LibHac](https://github.com/Thealexbarney/LibHac)**: reads NSPs and layers updates over the base game in `dm/tools/nx_extract`.
- **[SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross)**: SPIR-V to Metal Shading Language.
- **[glslang](https://github.com/KhronosGroup/glslang)** compiles the GLSL of OpenGL games to SPIR-V for it.
- **[MacNativeGames](https://github.com/ly05010419/MacNativeGames)** (MIT): the GPU widening of 8-bit indices and in-pass clears in `dm/gfx/nvn/nvn_mtl.m`, and the visibility-counter lowering in the shader translator.

## License

MIT ([LICENSE](LICENSE)). Third-party code keeps its own license ([dm/third_party](dm/third_party/README.md)).
