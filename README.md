# hp-p1005-print

Lightweight C tools for running an HP LaserJet P1005 over a Linux print server (CUPS + p910nd) and printing two-sided PDFs from Linux clients.

## What is included

- `printer-recover.c`: Server-side daemon that binds `usblp`, uploads the printer firmware (`sihpP1005.dl`) when needed, and manages `p910nd` and the CUPS queue. It checks whether a print job is active, `/dev/usb/lp0` is held open, or port 9100 has an established connection before touching the USB device, preventing mid-job cutoffs.
- `foo2xqx-wrapper.c`: Server-side CUPS filter that renders PDF/PostScript via Ghostscript (`pbmraw`, 1200x600) and pipes it to `foo2xqx` at maximum toner density (`-T5`). Fails the CUPS job on render errors so `ErrorPolicy retry-job` can recover.
- `duplex-print.c`: Client-side manual duplex utility for PDFs. Prints odd pages first, prompts via `zenity` to reinsert the stack as-is (blank side up, same top edge), then prints even pages in reverse order rotated 180 degrees. Pads odd page counts with a blank page so backs line up.
- `printer-recover.service`: Systemd unit file for `printer-recover`.
- `Makefile`: Builds static binaries for all three tools.

## Prerequisites

Server (Linux host connected to the printer via USB):
- `cups`, `p910nd`, `ghostscript`, `foo2zjs` (`foo2xqx` and `usb_printerid`), and firmware file `/var/lib/foo2zjs/firmware/sihpP1005.dl`.

Client (Linux desktop):
- `cups` (`lp`, `lpstat`), `qpdf`, `poppler-utils` (`pdfinfo`), `ghostscript` (`gs`), and `zenity`.

Build host:
- `gcc` and `make` (binaries are built with `-static` so the server does not need a compiler).

## Build and test

```sh
make
./duplex-print --selftest
```

## Server installation

1. Copy `printer-recover` to `/usr/local/sbin/printer-recover` (`chmod 755`).
2. Copy `printer-recover.service` to `/etc/systemd/system/printer-recover.service`, then run:
   ```sh
   sudo systemctl daemon-reload
   sudo systemctl enable --now printer-recover
   ```
3. Copy `foo2xqx-wrapper` to `/usr/lib/cups/filter/foo2xqx-wrapper` (`chmod 755`, owned by `root:root`) and point your PPD `*cupsFilter` lines to it.
4. Set `ErrorPolicy retry-job` in `/etc/cups/cupsd.conf` and `printers.conf`.

## Client installation and usage

Install `duplex-print` to `~/.local/bin/duplex-print` (or `/usr/local/bin/duplex-print`):

```sh
duplex-print document.pdf
```

Dry-run mode (generates `front.pdf` and `back.pdf` in `/tmp/duplex*` without printing):

```sh
duplex-print document.pdf --dry-run
```

Environment variables:
- `DUPLEX_PRINTER`: CUPS queue name (default: `HP_LaserJet_P1005`).
- `DUPLEX_ROTATE_BACKS`: Rotation in degrees for back pages (default: `180`, set to `0` to disable).
