# Cheats

TOTKDroid reads Atmosphère cheat files, the format Switch homebrew and the emulators use. It ships no cheat files.
A cheat file goes into a mod, like any other mod (Mods page; see the README).

## Status

- **Data cheats run** (first step): a cheat that only writes the game's memory (heap, main's data, pointer chains,
  button conditions). They run in the cheat VM every presented frame while switched on, from the game's 60th frame.
- **Code cheats are listed, greyed out** ("Changes the game's code: not supported yet."). Running them means changing
  the game's code, which is the second step and not started.
- A data cheat never writes into main's code: such a write is skipped (the log says so), and the rest of the cheat runs.
- Not run: opcode C4, the 0xF extensions, and opcode 5 with a register offset. These are listed with a note.
- Not verified yet: the data cheats in the recompiled build (`LARECOMP_NATIVE=0`, `totkgame.so`). They use the same
  memory and the same present hook, but the recompiled game may keep some of its data outside main's image.

## Adding a cheat file

1. Find the cheat file for the game's version. The file is named after the first 8 bytes of main's build id, in
   upper-case hex. The build id of TOTK 1.4.3 (the version TOTKDroid supports) is `277178B7DBA1B6D4`, so its file is
   **`277178B7DBA1B6D4.txt`**. A file with another name is for another build and is ignored.
2. Put it in the `cheats` folder of a mod (a folder of its own in `mods`):

   ```
   mods/
     My cheats/
       cheats/
         277178B7DBA1B6D4.txt
   ```

   A download laid out for the Switch's SD card or an emulator works as it is: the folder may hold
   `atmosphere/contents/0100F2C0115B6000/cheats`, `contents/0100F2C0115B6000/cheats` or `0100F2C0115B6000/cheats`.
   The names' case does not matter.
3. Mods page > Cheats: switch the cheats on. A data cheat applies at once while the game runs.

A mod that is turned off contributes no cheats. The cheat list is read when the game starts. While the game is not
running, the Mods page re-reads it, so it shows the current mods. Once the game runs, turning a mod on or off applies
at the next start.

## The file format

Standard Atmosphère: a `[name]` line starts a cheat, `{name}` a master cheat that is always on, and the cheat's
opcodes follow as 8-digit hex words. A name with no opcodes is shown as a heading. The title line (the one with
`TID=`) is not shown.

```
[Title line with TID= and BID=: not a cheat, not shown]

[A heading: a name with no opcodes]
[A cheat name]
<8-digit hex opcode words>
{A master cheat: always on}
<8-digit hex opcode words>
```

Each cheat's switch is stored in the settings as `cheats_on`: the FNV-1a 32 hash of `<mod folder name>/<cheat name>`
(with `#2`, `#3` ... for a repeated name in one file). Renaming a mod's folder resets its cheats to off.

## Code

- `runtime/cheat_vm.c`: the VM (opcodes 0-A and C0-C3, as Atmosphère's dmnt) and the parser. It has no platform
  code, so it is tested on the host: `tests/cheat_vm/run.sh`.
- `runtime/cheats.c`: finds the mods' files, classifies the cheats, reads and writes the game's memory (through the
  kernel's region table, `kernel_region_of`), and runs the VM from `nvnQueuePresentTexture`.
- `runtime/ui/mods_tab.mm`: the Cheats section of the Mods page.

The VM and the parser are ported from [valentderah/totk-pc-recomp](https://github.com/valentderah/totk-pc-recomp)
(MIT), `runtime/cheats.c` there.
