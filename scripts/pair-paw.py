#!/usr/bin/env python3
"""Compatibility entry point: pairing is now entirely wireless and physical."""
import sys

def main():
    print("USB key provisioning has been retired. Hold helmet BOOT for 2 seconds "
          "and all three paw buttons for 3 seconds after boot. Release, compare "
          "all six LED digits, then hold each pairing gesture again for 1 second "
          "only if they match. See firmware/esp32/README.md.", file=sys.stderr)
    return 2

if __name__ == "__main__":
    raise SystemExit(main())
