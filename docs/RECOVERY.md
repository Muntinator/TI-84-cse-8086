# Munt386 recovery and safety

Replacing calculator firmware is inherently risky. These rules are mandatory and
are enforced in both the firmware design and the release process.

## Absolute rules

1. **Never erase or overwrite the original firmware automatically.** Munt386
   flashes nothing during boot or normal operation.
2. **Never make flash changes without an explicit, deliberate user action**, and
   only after the replacement has been independently tested.
3. **Never ship an unrecoverable flashing process.** Every experimental build
   must document how to get back to a working calculator.
4. **Holding ON at power-up enters recovery mode**, never the experimental
   payload.

## Recovery modes

| Mode | Entry | Behaviour |
| --- | --- | --- |
| Normal | Power on, nothing held | Run the Munt386 payload (currently: diagnostic screen). |
| Diagnostic | Hold ON during power-up | Full hardware self-test with measured RAM size and flash check; no guest execution. |
| Recovery | Hold ON + a second documented key | Print recovery instructions and the firmware version; wait for a host-side recovery transfer. |
| Safe fallback | Any self-test failure | Print the failing subsystem, halt; do not continue. |

## Restoring the original firmware

The TI-84 Plus CSE can be restored using legitimate TI tooling and a legally
obtained OS upgrade file (the `.8cu` format). A 2-key recovery or the device's
own recovery prompt can be used to re-flash. Munt386 does **not** bundle or
provide any TI OS file.

## Version display

Every build prints `MUNT386 <version> <git-hash>` on the diagnostic screen and
into the guest-visible BIOS banner, so a device in the field can be identified.

## Development workflow safety

- Development happens on the **host emulator** first; the CSE image is only
  deployed once the host tests pass.
- Never commit a firmware image that has not passed `make test`.
- Never test destructive flash operations on the calculator without a validated
  recovery path.
