#!/bin/sh
# Offline known-answer smoke test; no captured traffic or network access.
set -eu
R=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$R"
T=$(mktemp -d "${TMPDIR:-/tmp}/poom-quality-handshake.XXXXXX")
trap 'rm -rf "$T"' EXIT
trap 'exit 1' HUP INT TERM
cat > "$T/esp_log.h" <<'STUB'
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
STUB
python3 - "$T" <<'PY'
import hashlib,hmac,sys
from pathlib import Path
p=Path(sys.argv[1])
ssid=b'POOM-QA-synthetic'
ap=bytes.fromhex('020000000001')
sta=bytes.fromhex('020000000002')
pmk=hashlib.pbkdf2_hmac('sha1',b'12345678',ssid,4096,32)
pmkid=hmac.new(pmk,b'PMK Name'+ap+sta,'sha1').digest()[:16]
(p/'synthetic.22000').write_text('WPA*01*'+pmkid.hex()+'*'+ap.hex()+'*'+sta.hex()+'*'+ssid.hex()+'***\n')
(p/'words.txt').write_text('short\nwrong-password\n12345678\n')
(p/'invalid.22000').write_text('not a handshake\n')
PY
${CC:-cc} -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-unused-variable \
    -fsanitize=address,undefined -I"$T" -Iapplications/poom_app_pack/include \
    -Itools/security_audit_hostcheck/stubs tools/quality_check/test_handshake.c \
    applications/poom_app_pack/src/poom_handshake_crack.c -o "$T/test"
"$T/test" "$T/synthetic.22000" "$T/words.txt" "$T/invalid.22000"
