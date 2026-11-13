# hp-p1005-print

Lightweight C tools for running an HP LaserJet P1005 over a Linux print server (CUPS + p910nd) and printing two-sided PDFs from Linux, Windows, macOS, and iOS (AirPrint).

## Files (9 total)

- `printer-recover.c`: Server-side daemon that binds `usblp`, uploads `sihpP1005.dl` when firmware is missing, and manages `p910nd` and the CUPS queue. Checks `lpstat -o`, `/proc/*/fd`, and `/proc/net/tcp` (port 9100) so it never touches `/dev/usb/lp0` while a job is active.
- `foo2xqx-wrapper.c`: Server-side CUPS filter that pipes Ghostscript (`pbmraw`, 1200x600) into `foo2xqx` at maximum toner density (`-T5`), failing the job on render errors so `ErrorPolicy retry-job` can recover.
- `duplex-print.c`: Client-side manual duplex utility for PDFs. Prints odd pages first, prompts via `zenity` to reinsert the stack as-is (blank side up, same top edge), then prints even pages in reverse order rotated 180 degrees. Pads odd page counts with a blank page so backs line up.
- `duplex-print.desktop`: Desktop entry so Linux file managers show "Print duplex (P1005)" in the right-click "Open With" menu for PDFs.
- `printer-recover.service`: Systemd unit file for `printer-recover`.
- `Makefile`: Compiles static binaries (`-O2 -Wall -Wextra -static`) for all three C programs.
- `README.md`: Setup, build, and client guides.
- `LICENSE`: MIT license.
- `.gitignore`: Excludes compiled binaries and object files.

## Prerequisites

Server (Linux host connected to the printer via USB):
- `cups`, `p910nd`, `avahi-daemon` (for AirPrint/mDNS), `ghostscript`, `foo2zjs` (`foo2xqx` and `usb_printerid`), and firmware file `/var/lib/foo2zjs/firmware/sihpP1005.dl`.

Linux client:
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
4. Set `ErrorPolicy retry-job` and `Browsing Yes` / `BrowseLocalProtocols dnssd` in `/etc/cups/cupsd.conf`, and `ErrorPolicy retry-job` in `printers.conf`.
5. Ensure `avahi-daemon` is running so macOS and iOS (AirPrint) discover the printer automatically.

## Client setup

### Linux (with two-sided PDF support)

1. Add the printer in CUPS pointing to `ipp://<server-ip>:631/printers/<queue-name>`.
2. Install the binary and desktop entry:
   ```sh
   install -m755 duplex-print ~/.local/bin/duplex-print
   install -m644 duplex-print.desktop ~/.local/share/applications/duplex-print.desktop
   ```
3. Print from a terminal (`duplex-print document.pdf`) or right-click any PDF in your file manager and choose **Open With -> Print duplex (P1005)**.
4. Dry-run without printing: `duplex-print document.pdf --dry-run`.

Environment variables:
- `DUPLEX_PRINTER`: CUPS queue name (default: `HP_LaserJet_P1005`).
- `DUPLEX_ROTATE_BACKS`: Rotation in degrees for back pages (default: `180`, set to `0` to disable).

### Windows

1. Open **Settings -> Bluetooth & devices -> Printers & scanners -> Add device -> Add manually**.
2. Choose **Select a shared printer by name** and enter `http://<server-ip>:631/printers/<queue-name>`.
3. When prompted for a driver, choose **Generic -> MS Publisher Imagesetter** (or **Generic PostScript**).
4. For manual two-sided printing, print **Odd pages only** first, reinsert the stack as-is (blank side up, same top edge), then print **Even pages only** with **Reverse pages** enabled.

### macOS

1. Open **System Settings -> Printers & Scanners -> Add Printer, Scanner, or Fax**.
2. Select the printer discovered via Bonjour/AirPrint (or add by IP using **Internet Printing Protocol - IPP**, address `<server-ip>`, queue `/printers/<queue-name>`).
3. For manual two-sided printing, use **Paper Handling -> Sheets to Print: Odd Only**, reinsert the stack as-is, then print **Even Only** in **Reverse** order.

### iPhone / iPad (AirPrint)

1. Connect to the same Wi-Fi network as the print server.
2. Open any document or photo, tap **Share -> Print**, and select the printer (advertised automatically by CUPS + Avahi).
3. AirPrint on iOS supports one-sided printing only because the P1005 has no hardware duplexer.
