# SC4 Load Cache

A SimCity 4 DLL plugin that makes opening a city faster. Plugins do not change while the game
runs, but every city load lists the keys of every plugin file about 13 times and loads every
exemplar several times only to read its type. SC4LoadCache remembers both for the game session.

Measured with a large plugins folder (Kanto Tokai region, plugins on OneDrive):

| | without | with SC4LoadCache |
|---|---|---|
| first city after starting the game | 30-32 s | 14.1 s |
| every city after that | 11.6 s | 1.9-4.4 s |

## What it does

- **Key lists by type.** The resource manager walks the index of every plugin file for each
  list it builds. The keys each plugin file holds are kept per type; later lists are put
  together from them in the game's order. City saves, loaded after start-up, are asked as
  before.
- **Exemplar scans.** Nine places in the game load every exemplar to keep only one exemplar
  type (Type 21 network props, automata, demand and others). The type of each exemplar is
  remembered, and an exemplar of another type is not loaded again. The first time, the type
  is read straight from the exemplar's record where that is certain, without loading it.
- **File reads.** The game moves the file position with a system call before every read, also
  when the bytes are already in its read buffer, and reads text files such as NAM's RUL files
  512 bytes at a time. A seek inside the buffer now only moves the position, and a file read
  straight through gets a 64 KB buffer.

All of it gives the game exactly what it would have read or built itself. Details, addresses
and the review notes are in [docs/re-notes.md](docs/re-notes.md).

## Install

Copy `SC4LoadCache.dll` into the Plugins folder. SimCity 4 1.1.641 for Windows only; on another
version it installs nothing. It writes `SC4LoadCache.log` next to itself.

## Build

Visual Studio 2022, CMake 3.24 or newer, 32-bit:

```
git submodule update --init
cmake -S . -B build -A Win32 [-DSC4_PLUGINS_DIR="<SimCity 4 Plugins folder>"]
cmake --build build --config Release
```

`ctest --test-dir build -C Release` checks the reader of exemplar records, and
`python tests/check_exemplar_types.py build/Release/exemplar_type_test.exe <folders>` runs it
over the exemplars of real plugin folders.

`SC4LoadProfiler.dll` is a test DLL: it times the phases of each city load and samples where the
game spends it (`SC4LoadProfiler.log`, `-samples-N.tsv`, `-stacks-N.tsv` next to it). Not for
normal play.

## License

LGPL-3.0-or-later, see [LICENSE.txt](LICENSE.txt). Uses gzcom-dll (LGPL-2.1) and MinHook
(BSD-2-Clause).
