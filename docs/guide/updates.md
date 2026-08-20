# Updating

The panel updates itself. You do not need a computer or the browser installer
again.

**Settings → Firmware Update → Check for Updates**

Pick **Stable** or **Nightly** in the channel dropdown. Stable is the default and
the right choice unless you want to help test.

The device knows its own screen size and downloads the matching build.

## If an update stops partway

It resumes. If the transfer stalls, the panel reconnects and carries on from the
byte it reached, rather than starting the whole image again. Seeing
`Resuming from 47%…` means the recovery is working — let it run.

If it still fails, see
[Troubleshooting](/TROUBLESHOOTING#updates-fail-or-stop-partway).

::: warning Do not unplug during an update
If a flash is interrupted the board keeps the previous firmware and will boot
into it, but wait for the panel to restart on its own.
:::
