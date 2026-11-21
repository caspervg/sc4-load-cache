# City load cache (SC4LoadCache.dll)

Windows 1.1.641. Found with the load profiler (`src/profiler`) and a read-only Ghidra pass.

## Measured (Kanto Tokai plugins, OneDrive)

| | cold load | warm load |
|---|---|---|
| total | 30-32 s | 11.6 s |
| `GetResourceList` (index walks) | 34 calls, 9-13 s | 13 calls, 3.5 s |
| Type 21 scan | ~4.5 s + list | ~4.5 s |
| first load only, at `kPostCityInit`: network RUL INI load (`cSC4NetworkTool::Init` `0x006269c0` -> `0x006253d0`, flag `0x00b452a8`; RUL2 overrides `0x0063d3e0` alone ~11 s), lot configurations (`0x0048a5e0`) ~2.4 s | 15.6 s | - |

## With the DLL (2026-11-21, same plugins)

Radisson, Joutel, Auclair, Joutel, Radisson after a fresh start, opened with `SeamlessOpenCity`:

| load | before | with SC4LoadCache |
|---|---|---|
| first (cold) | 30-32 s | 23.4 s |
| later | 11.6 s | 1.9-4.3 s |
| `GetResourceList`, later load | 13 calls, 3.5 s | 13 calls, 30 ms |

SC4DBPFLoading packs the plugins into 13 segments at start-up. Lists built per session:
exemplars 192919 keys (11 calls per load), textures 2602374 keys (once per load, not kept
over 500000 keys), three small ones. Scan callbacks skipped: 7.2 million, passed 288000.

Later runs, same cities (one run each; first loads vary by a second or two):

| version | first load | later loads |
|---|---|---|
| + seek inside the buffer, type and group lists | 15.8 s | 1.9-4.5 s |
| + 64 KB buffer for every file after 8 refills | 18.7 s | 2.1-4.7 s |
| + 64 KB buffer only after 8 refills in a row | 14.1 s | 2.2-4.4 s |

Growing every busy file made the first-load exemplar scan (`0x004a4310`) 2.5 times slower:
plugin files are read at random places, so each refill read 64 KB for one small record.

## Key lists

- `ResourceManager::GetResourceList` `0x009739cb`, thiscall `(list** out, filter*)` -> key count
  (summed over segments, so duplicates count twice). Vtable slot +0x44 of the manager
  (vtable `0x00ad89d0`). Takes the manager's critical section at `rm+0x80` (lock slot +4,
  unlock +8), makes a key list (`operator new` `0x005e55e0`, 0x40 bytes, ctor `0x00974e7a`,
  vtable `0x00aada64`) and calls slot +0x48 `(list, filter)` of every segment.
- `GetResourceListForType` `0x00973280` (slot +0x48): wraps the type in a filter (12 bytes,
  vtable `0x00ad8970`, type at +8) and calls slot +0x44.
- Locks: `rm+0x80` guards resources (`GetResource`, `GetResourceList`); `rm+0x50` guards the
  segment deque (register front +0x6c `0x0097437b`, register back +0x70 `0x009743c1`, used for
  city saves in `LoadCity` `0x00455743` and new city `0x00454b88`; unregister +0x74
  `0x009742b7`; `FindDBSegment(key, seg**)` +0x80 `0x0097377f`, first segment in deque order
  with the record, AddRef'd). The game takes +0x80 before +0x50. `GetResourceList` itself walks
  the deque under +0x80 only.
- Plugin segments are registered once, in `SetupResources` `0x004572b0` (one call site,
  `0x0044c97e`); there is no later plugin folder.
- Segments: a deque of segment pointers, begin iterator `{cur, first, last, node}` at `rm+0x58`,
  end `cur` at `rm+0x68`, 32 pointers per buffer (`0x0097316e` increments).
- Key list (vtable `0x00aada64`): a deque of 12 byte keys at +0x18, insertion order,
  duplicates kept (a key in two segments is listed twice). +0xc `Insert(list)`, +0x10
  `Insert(key)` (push_back), +0x18 `EraseAll`, +0x1c `EnumKeys(callback, context)` (calls
  `callback(&copy, context)` cdecl), +0x24 `Size`.
- Type filter `0x00ad8970`: `IsKeyIncluded` (`0x00973194`) compares only `key.type`.
- The gzcom-dll header of `cIGZPersistResourceManager` and `cIGZPersistDBSegment` is two slots
  short before these entries on Windows; only the slots above are used.

## Exemplar scans

Each calls `rm->GetResourceListForType(&list, 0x6534284a)` (every exemplar) and
`list->EnumKeys(callback, context)`. Every callback below is cdecl `(const key*, context)`,
does `rm->GetResource(key, 0xa52160f5, &res, 0, 0)` (slot +0xc), `holder = res->slot+0x1c()`,
`0x005fd480(holder, 0x10, &type)` (cdecl, bool), and when the type is not its own only
releases `res` and returns (checked instruction by instruction):

| Callback | Scan (caller) | Takes ExemplarType |
|---|---|---|
| `0x0060fb90` | network props `0x006100d0`, every load (~4.5 s) | 0x21 |
| `0x00513a60` | automata `ScanExemplars` `0x00514520`, 4 per load | `[context+0xa40]` |
| `0x006a7840` | `cSC4DemandSimulator::Init` `0x006a8c20` | 3 |
| `0x006b03b0` | building development `0x006b0760` | 0xf |
| `0x00699e20` | `0x0069d240` | 4 |
| `0x0069d130` | `0x0069d1d0` (once, flag `0x00b48540`) | 2, 0x11, 0x15 |
| `0x0078cb40` | `0x0078cce0` (returns at once when flags `0x00b4dfd2`/`d1` are set) | 0x29 |
| `0x0080e430` | `0x0080e4a0` | 0xe |
| `0x004a42a0` | `0x004a4310` | 0x1e |

Not filtered: `0x0043b710` and `0x00489990` test the key's group first; `0x0078cc70` and the
`0x00682020` users take another path. Whichever scan runs first in a load pays the disk reads
for all exemplars; the others hit the game's cache. So skipping one scan alone only moves
the cost: all of them have to skip.

City saves hold exemplars too (Auclair: 237 records of type 0x6534284a).

## What the DLL does

- Key lists by type filter: the first list of a type is built as the game does, noting the run
  of keys each segment registered before `PostAppInit` added. Later lists of that type are put
  together in the game's segment order: the noted runs for start-up segments, a live call for
  segments added since (city saves). The same keys in the same order, and a new list each time.
- Exemplar scans: the type of each exemplar is noted when a callback first sees it (read
  again after the game's callback, from its cache), only when the record the game uses is in
  a start-up segment (saves differ per city) and it loaded. Later, a callback is skipped for an
  exemplar of a type it does not take.
- Start-up segments get one reference each, so no later segment can reuse their address.

## Further ideas (not done)

- `cRZFile::Seek` `0x0091895a` (vtable `0x00ad1e58` +0x34) calls `SetFilePointer` every time,
  also for an absolute seek inside the read buffer (`[+0x60, +0x60+[+0x64])`, position
  `+0x48`). The INI line reader `0x0091ca15` seeks back after every read. ~5800 of 16000
  samples on a cold load (most in the RUL load), ~1300 of 6300 warm. Read-only handles only.
- Type+group filter lists (filter vtable `0x00aaf4e0`, from `0x00682020`; users `0x0063f700`,
  `0x006a1450`, `0x0074f410`, `0x0074fdc0`) walk the index on the first load (~2.7 s): could
  come from the kept exemplar runs.
- Keep the exemplar type index on disk, keyed by the plugin files' names, sizes and times.

## ExemplarType from the raw record

The first exemplar scan of a session (`0x004a4310`, callback `0x004a42a0`) built every exemplar
once, about 3 s of a 10.3 s first load. Where that went (1653 samples of 1.85 ms, each given to
the innermost of these):

| | samples | |
|---|---|---|
| `ReadFile` in the file's buffer refill `0x00918f67` | 701 | records under the buffer size |
| `SetFilePointer` in `cRZFile::Seek` | 113 | |
| record read, rest (`0x0096e1e5`; SC4DBPFLoading reads larger records in one call) | 83 | |
| text exemplar entries (`0x0067f0d0`, `0x0067e140`, `0x0067e250`) | 351 | a third of the exemplars are text |
| `SerialRead` `0x0067f1e0`, rest | 221 | |
| QFS unpacking `0x0096e6f4` | 11 | |
| the rest (segment lookup, factory, cache) | 173 | |

So building the exemplar is about a third and reading the record about half. The baseline
profiles from before the 64 KB read buffer had 330-350 samples in the buffer refill for the
same scan, against 701 here. A file whose buffer has grown reads 64 KB for every record under
64 KB that is not in the buffer already (SC4DBPFLoading only reads a request in one call when
it is at least the buffer size), and 216 buffers had grown in that session. Not measured apart.

### How the game gets the type

- `GetResource` `0x009732eb`: lock `rm+0x80`; ask every cache at `rm+0x88`; if none has it,
  slot +0xb8 `0x00972eae`: `FindObjectFactory` (+0x58 -> +0x54 by type), `FindDBSegment`
  (+0x80), then +0xbc `0x00972f1d`: `segment->OpenRecord(key, &record, 1)` (+0x54),
  `factory->CreateInstance(record, ...)` (+0xc, `0x0097554f`), `segment->CloseRecord(record)`
  (+0x60) and `record->Release()`, or `segment->AbortRecord(&record)` (+0x64) when it failed.
  The loaded resource goes to the caches (+0xb0).
- `OpenRecord` of a packed file (`0x009701fd`): a record the file's directory record lists is
  read and unpacked into memory (`0x0096e6f4`); another one up to `[segment+0xd0]` bytes is read
  into memory (`ReadFileSpan` `0x0096e1e5`: seek, read); a larger one is read from the file as
  it goes. Record interface (vtables `0x00ad82a8` in memory, `0x00ad80a8` from the file): +0xc
  `GetKey`, +0x18 `GetFieldVoid(buffer, size)` (fails without reading when fewer bytes are
  left), +0x20 `GetSize`, +0x2c `SeekAbsolute`. `QueryInterface(0xc56b8f08)` gives the serial
  record: +0x10/+0x14 Uint8 (array, one), +0x30/+0x34 Uint32, +0x60 String, +0x64 ResKey
  (three Uint32), +0x68 Variant, +0x6c Void, +0xd0 `GetSize`, +0xd8 `GetPosition`.
- The factory (`0x0097554f`) makes the resource for the key's type (exemplar factory vtable
  `0x00aaeef8`, `CreateInstance` `0x0067d820`, object 0x40 bytes, ctor `0x0067cc50`) and calls
  `SerialRead` (+0x28).
- `cSCResExemplarFactory::SerialRead` `0x0067f1e0`: reads 8 bytes and compares the first four.
  - `EQZB`: parent cohort key (`GetFieldResKey`); when it is not 0,
    `GetResource(parent, 0x452160fb)`, and **the exemplar does not load when that fails**;
    `SetCohort` (+0x28); `RemoveAllProperties(false)`; a Uint32 count; then per property a
    Uint32 id and a byte. Byte 0: `GetFieldVariant`, then `AddProperty` (holder +0x3c) unless
    the id is in the factory's set at +0x14 (empty; nothing found that adds to it). Byte 1: a
    resource key, loaded with `GetResource(key, 0x456b8f1d)`, and the exemplar fails when that
    fails. Any other byte, or any read that fails: the exemplar does not load. Bytes after the
    last property are not looked at.
  - `EQZT`: two more bytes are skipped, the rest of the record is the text (`0x0067cac0`).
    Lines end at `\n`, one `\r` before it is dropped, empty lines are skipped (`0x0067c240`); a
    line is split at its first `=` and both parts lose spaces and tabs at their ends
    (`0x0067e0b0`); a line without `=` fails. The first entry must be named `ParentCohort`, the
    second `PropCount` (`_stricmp`), then that many properties: id = the number at the start
    of the name, value by `0x0067e780`: type name up to the first `:` (`Uint32`, `Sint32`,
    `Float32`, `String`, ..., case matters), count up to the next `:`, `{`, values. With count
    0 a single value, else an array.
  - Numbers (`0x0090fe93`): `strtoul` in base 16 after `0x`, or when a letter a-f comes before
    the first character that is no hex digit; else base 10. Too large gives 0xFFFFFFFF.
  - At the end `GenerateCachedMap` (+0x3c, `0x0067ddc0`) and `CompactProperties` (+0x34).
- `GetFieldVariant` `0x0097621f`: a Uint32 code, then 1 Uint8, 2 Uint16, 3 Uint32, 4 Uint64,
  5 Sint8, 6 Sint16, 7 Sint32, 8 Sint64, 9 Float32, 0xa Float64, 0xb Bool, 0xc Char, 0 nothing;
  0x8000 + the same numbers: a Uint32 count and that many values (0x8000 bytes, 0x8010 two
  bytes each, 0x8011 one); 0x800c a string (`0x00977054`: a Uint32 length, the top byte 0, or
  0x10 for two bytes per character; over 200000 fails; other top bytes read to the end of the
  record). Any other code fails. This is the usual "value type, key type, rep count" header:
  `00 03 00 00 00` is byte 0 and code 3, `00 03 80 00 00` byte 0 and code 0x8003.
- Properties: `AddProperty` (`0x0067dce0`, base `0x005ff6d0`) keeps a vector sorted by id and
  **replaces** an entry with the same id, so the last one in the record wins.
  `GenerateCachedMap` puts the parent cohort's properties in the map the getters use
  (`exemplar+0x34`) and then the exemplar's own over them. `GetProperty(id)` (holder +0x24,
  `0x0067c670`) is a lookup in that map.
- `GetPropertyValueAsUnsignedLong` `0x005fd480`: `holder->GetProperty(id)` (+0x24),
  `->GetPropertyValue()` (+0x14), `->GetValUint32(&out)` (variant +0x64, `0x009176c8`): the
  value when the variant is a single Uint32 (type 6), else whatever `ConvertTypes`
  `0x00917350` makes of it (arrays go through a string).

### What the DLL does

For an exemplar it has no type for, a scan hook opens the record the way `0x00972f1d` does
(under `rm+0x80`, `FindDBSegment`, `OpenRecord`, `CloseRecord`), copies its bytes and walks
them (`src/loadcache/ExemplarType.h`): every property up to the count, keeping the last
property 0x10. It gives a type only when that property is the exemplar's own and a single
Uint32 (code 3, or `Uint32:0:{...}` in text). Everything else is left to the game: no property
0x10 (parent cohort), another variant code, a record that ends early, an unknown code, a text
number `strtoul` would read after blanks or a sign, a record in a segment added after start-up.

Why the scans see the same: a scan takes an exemplar when it loads and its type is the scan's.
The walk reads the record as `SerialRead` does, so when the exemplar loads, its type is the
one found; when it does not load (a missing parent cohort, a broken later property), no scan
takes it whatever type is remembered. A scan that takes the type still calls the game's
callback, which loads the exemplar itself.

SC4ResourceLoadingHooks registers its own exemplar factory around the game's and, after a
load, adds the properties of exemplar patches (cohorts of group `0xb03697d1`, except
properties `0x0062e78a` and `0x20`) to the exemplar. A patch with a property 0x10 would change
the type the game reads, so at the first exemplar the DLL looks at every patch cohort
(`GetResource(key, 0x25216283)`, `GetProperty(0x10)`) and reads no raw types that session when
one has it. None of the 312 patches in the test plugins has. A DLL that changes property 0x10
in an exemplar load hook is not seen; no DLL in the test plugins uses those hooks.

Exemplars of a type no scan takes are no longer built during the first load, only when
something asks for them.

### Checks

- `ctest`: hand-made records (`tests/exemplar_type_test.cpp`).
- `python tests/check_exemplar_types.py <exemplar_type_test.exe> <folders>`: every exemplar of
  real DBPF files through the reader, a second reader written from these notes and one that
  only knows the usual format description. Plugins and game folder, 2026-11-21: 201876
  exemplars (134483 binary, 67390 text), 198073 answered (98.1%), 2738 without a type of their
  own, 1056 with the type as an array of one, 6 other text forms, 3 not exemplars; no
  differences. The walk over all of them takes under 0.4 s in the test program, reading the
  records from a file and printing included.
- In the game: an empty file `SC4LoadCache.verify` next to the DLL. The raw type is then not
  used; every exemplar loads as before and the two types are compared, with a line in
  `SC4LoadCache.log` for each difference (`DIFFERENT`), for exemplars that have a raw type but
  did not load, and a count every 20000 exemplars and at exit. Not run yet.
