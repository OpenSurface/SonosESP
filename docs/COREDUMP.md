# Reading a crash off the device

The firmware writes an ELF core dump to flash on every panic. This has always been
on — `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`, ELF format, CRC32-checked, into a
64 KB `coredump` partition at `0xFF0000` — but nothing read it back until v1.15.3.

Two things changed:

- **`setup()` now reports a stored dump** on every boot: size, offset, faulting
  task, PC, and the RISC-V trap registers. That much lands in any serial log a
  user pastes into an issue, with no tooling.
- **This page** is how to get the full symbolised backtrace.

The dump is **not** erased after being reported. It survives reboots and is only
overwritten by the next panic, so there is no rush to collect it — but reflashing
a *different build* will make the symbols useless (see the ELF warning below).

## What you get for free

If the device has crashed, its next boot prints something like:

```
[BOOT] Reset reason: PANIC (4) - CRASH: exception or assert
[BOOT] *** PREVIOUS RUN ENDED ABNORMALLY ***
[COREDUMP] *** A CRASH FROM A PREVIOUS RUN IS STORED ON THIS DEVICE ***
[COREDUMP] 12480 bytes at flash offset 0xFF0000
[COREDUMP]   task   : SonosPoll
[COREDUMP]   PC     : 0x400D2A1C
[COREDUMP]   mcause : 0x00000005   mtval : 0x00000000
[COREDUMP]   ra     : 0x400D29F0   sp    : 0x3FCA1B20
```

`mcause` is the RISC-V trap cause. The ones worth recognising:

| mcause | Meaning |
|---|---|
| `0x2` | Illegal instruction — often a corrupted function pointer |
| `0x5` | Load access fault — read from a bad address (null/freed pointer) |
| `0x7` | Store access fault — write to a bad address |
| `0x1` | Instruction access fault — jumped somewhere invalid |

A load/store fault with `mtval` at or near `0x00000000` is a null dereference.

## Full backtrace

Install the tool once:

```bash
python -m pip install esp-coredump
```

Then read the dump straight off the device, symbolised against the firmware:

```bash
python -m esp_coredump --chip esp32p4 --port COM9 \
    info_corefile .pio/build/esp32_4inch/firmware.elf
```

Use `esp32_7inch` for the 7" build, and your own serial port.

### The ELF has to match the build that crashed

`firmware.elf` is what turns addresses into function names and line numbers. It
must be the **exact build that was running when the crash happened**. If you have
rebuilt or reflashed since, the addresses will resolve to the wrong symbols and
quietly produce a plausible, wrong backtrace.

The dump records the crashing build's `app_elf_sha256`, so `esp_coredump` will
warn on a mismatch — do not ignore that warning.

If the build is gone, the reset reason and the register dump above are still
valid; only the symbol names are lost.

### Saving the raw dump

To archive it before it is overwritten, or to hand it to someone else:

```bash
python -m esp_coredump --chip esp32p4 --port COM9 \
    info_corefile -s crash.elf .pio/build/esp32_4inch/firmware.elf
```

`-s` writes the core file out. Attach that plus the matching `firmware.elf` to an
issue and it can be decoded anywhere.

## Reading it without the serial console

The console is USB CDC, which means the serial port belongs to the chip. A reset
tears the port down, so anything the panic handler printed on its way out is lost
before it reaches the host — which is exactly why a spontaneous reboot shows a
clean log, a dropped port, and a fresh boot with no explanation.

The core dump is written to flash *before* that happens. It is the only account of
the crash that survives, and it is why the boot-time report exists.

## If there is no dump

`[COREDUMP]` lines absent on boot means no dump is stored. Combined with the
`[BOOT] Reset reason:` line, that narrows things considerably:

| Reset reason | No dump means |
|---|---|
| `POWERON` | Power was applied or the button was pressed. Not a crash. |
| `BROWNOUT` | The supply dipped. Cable, PSU, or USB port — **not firmware**. |
| `TASK_WDT` / `INT_WDT` | A hang, not an exception. Watchdogs do not write dumps. |
| `PANIC` | A crash that failed to write its dump — flash busy, or the panic hit inside the dump writer. |
| `SW` | A deliberate `esp_restart()` — OTA or a settings change. |
