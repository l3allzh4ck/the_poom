# POOM handshake receiver (Hackberry Pi)

Catches `.22000` captures pushed from the POOM **HS VAULT** app and auto-runs
`hashcat -m 22000` against a wordlist.

**Scope:** for cracking handshakes captured on **your own gear or in a consented
CTF**. Don't point it at third-party captures.

## The contract
HS VAULT sends:

```
POST http://<pi-ip>:<port>/upload?name=<file>.22000
Content-Type: application/octet-stream
<raw .22000 bytes as the body>
```

The receiver saves the file (safe basename only, never overwrites), then launches
hashcat in the background and returns JSON: `{"ok":true,"saved":"...","log":"..."}`.

## Run it on the Pi
```bash
# hashcat must be installed and on PATH (sudo apt install hashcat, or your build)
python3 poom_handshake_receiver.py
```
Defaults: listens on `0.0.0.0:8080`, saves to `~/poom_handshakes/`, cracks against
`/usr/share/wordlists/rockyou.txt`.

Override anything via env vars (no code edits):

| Env var | Default | Meaning |
|---|---|---|
| `POOM_RX_PORT` | `8080` | listen port |
| `POOM_RX_DIR` | `~/poom_handshakes` | where captures + logs are saved |
| `POOM_RX_WORDLIST` | `/usr/share/wordlists/rockyou.txt` | wordlist for `-a 0` |
| `POOM_RX_HASHCAT` | `hashcat` | hashcat binary / path |
| `POOM_RX_HASHCAT_EXTRA` | *(empty)* | extra args, e.g. `-w 3 -O` |
| `POOM_RX_POTFILE` | `~/poom_handshakes/poom.potfile` | cracked-results potfile |
| `POOM_RX_AUTOCRACK` | `1` | set `0` to only save, not crack |
| `POOM_RX_MAX_BYTES` | `1048576` | reject uploads larger than this |

## On the POOM
`THE BEAST > HS VAULT` → pick a handshake → **A** (details) → **A** (send).
Set the endpoint once with **RIGHT** in the details screen (persisted in NVS):
```
http://<your-pi-ip>:8080/upload
```
The POOM joins a saved WiFi profile (shared with Settings > WIFI PROFILES) to
reach the Pi.

## Check results
```bash
# which handshakes cracked, read from the potfile:
hashcat -m 22000 ~/poom_handshakes/<file>.22000 --show --potfile-path ~/poom_handshakes/poom.potfile
# or tail a specific run:
tail -f ~/poom_handshakes/<file>.22000.log
```

## Kali note
On Kali the wordlist often ships compressed, which hashcat can't read as-is:
```bash
ls -l /usr/share/wordlists/rockyou*
sudo gunzip /usr/share/wordlists/rockyou.txt.gz   # only if you see rockyou.txt.gz
```
On an ARM SBC (Raspberry Pi) hashcat may report "No devices found" without an
OpenCL backend: `sudo apt install pocl-opencl-icd` (CPU OpenCL).

> Bind it to your LAN only. It accepts a file and starts a cracking process, so
> don't expose `:8080` to the open internet.
