# SilentPatch

<p align="center">
  <img src="https://i.imgur.com/sCDzq12.png" alt="Logo">
</p>

SilentPatch for the 3D-era Grand Theft Auto games is the first and flagship release of the "SilentPatch family", providing numerous fixes for this beloved franchise.
SilentPatch addresses a wide range of issues, from critical fixes for crashes and other blockers to various major and minor improvements identified by
the passionate community in these games over decades. SilentPatch does not alter the core gameplay experience, making it an optimal choice
for both first-time players and the old guard returning for yet another playthrough.

This fork supports **GTA San Andreas v1.0 (US and EU) only**.

## Featured fixes

* [Fixes in GTA San Andreas](CHANGELOG-SA.md)

## Compilation requirements

* Visual Studio 2017 or newer (MSVC x86 toolset).
* [xmake](https://xmake.io/) 3.0.1 or newer.

All dependencies are vendored in this repository, so no submodules, vcpkg, environment variables or downloads are required:

* ModUtils headers (`SilentPatch/Utils`).
* RenderWare Graphics 3.6 headers and import libraries (`third_party/rw3603`).
* [miniaudio](https://github.com/mackron/miniaudio) for streamed FLAC user tracks (`third_party/miniaudio`).

To build:

```
xmake f -a x86 -m release -y
xmake -y
```

The output is `build/windows/x86/release/SilentPatchSA.asi`.

## Contribution guidelines

* Contributions with bug fixes are welcome, but you must be able to explain why you believe they fix a bug, rather than alter a design decision that doesn't suit you.
  I reserve the right to reject submissions that cannot be unambiguously classified as fixes.
* Contributions must support GTA San Andreas v1.0 (US and EU). Other game versions are out of scope for this fork.
* This repository is not intended for game support. Issues such as "I installed mods and the game now crashes" will be closed.

## Credits

SilentPatch includes code contributions from:
* aap
* B1ack_Wh1te
* CanerKaraca
* DK22Pac
* Fire_Head
* iFarbod
* Kaizo M
* Nick007J
* NTAuthority
* rx
* Sergeanur
* sndth
* spaceeinstein
* Wesser
