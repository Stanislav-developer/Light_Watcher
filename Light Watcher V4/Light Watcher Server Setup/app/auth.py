import hashlib
import hmac
import re

EPOCH_MIN = 1_600_000_000
N_RE = re.compile(r"^([0-9a-f]{8})\.([0-9]{1,10})$")


def hmac_hex(key: str, msg: bytes) -> str:
    return hmac.new(key.encode(), msg, hashlib.sha256).hexdigest()


def derive_key(token: str, dev: str) -> str:
    return hmac_hex(token, b"lw3:" + dev.upper().encode())


def sign_request(key: str, ts: str, n: str, body: bytes) -> str:
    return hmac_hex(key, ts.encode() + b"." + n.encode() + b"." + body)


def sign_response(key: str, n: str, body: bytes) -> str:
    return hmac_hex(key, n.encode() + b"." + body)


def verify(expected: str, sig: str) -> bool:
    return hmac.compare_digest(expected, sig.strip().lower())


def parse_n(n: str) -> tuple[str, int] | None:
    m = N_RE.match(n.strip())
    if not m:
        return None
    return m.group(1), int(m.group(2))


def ts_fresh(ts: str, now: float, window: int) -> bool:
    try:
        value = int(ts)
    except ValueError:
        return False
    if value < EPOCH_MIN:
        return value >= 0
    return abs(value - now) <= window
