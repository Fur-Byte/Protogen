"""Independent hashlib/hmac oracle. Public synthetic test fixtures only.

Run --check in validation. Updating vectors is an explicit developer action.
"""
import hashlib
import hmac
import json
from pathlib import Path
import struct
import sys

PATH = Path(__file__).with_name("vectors.json")
PAW = bytes.fromhex("020000000003")
HELMET = bytes.fromhex("020000000004")
WIRE = 0x123456789ABCDEF0
SIZES = [129, 129, 1568, 1568, 16, 16]
ROLES = [2, 1, 1, 2, 2, 1]

def header(i):
    n = SIZES[i]
    return (bytes([0x70, 4, 10, ROLES[i], 2, i, 0, (n+67)//68]) +
            struct.pack("<H", n) + b"\0\0" + struct.pack("<Q", WIRE) +
            (PAW if ROLES[i] == 2 else HELMET) + b"\0\0")

def hkdf(root, salt, label, size):
    prk = hmac.digest(salt, root, "sha256")
    return hmac.digest(prk, label.encode()+b"\x01", "sha256")[:size]

def build():
    objects = [bytes((j+i*17) % 256 for j in range(n)) for i,n in enumerate(SIZES)]
    transcript = b"protogen/v4/transcript\0" + bytes([0x70,4,2,2,1,1,0,4]) + struct.pack("<Q", WIRE)
    transcript += b"".join(header(i)+hashlib.sha256(o).digest()+o for i,o in enumerate(objects))
    th = hashlib.sha256(transcript).digest()
    ecdh, kem = bytes(range(1,33)), bytes(range(33,65))
    fi = b"protogen/v4/hybrid/p256-mlkem1024/sha3-256\0" + struct.pack(">II",256,len(transcript)) + transcript
    root = hashlib.sha3_256(b"\0\0\0\x01"+ecdh+kem+fi).digest()
    labels = [("paw-to-helmet/aes-gcm",32),("helmet-to-paw/aes-gcm",32),("id",8),("recovery",32),
              ("esp-now/lmk",16),("esp-now/pmk",16),("identity/paw",6),("identity/helmet",6),
              ("finished/paw",32),("finished/helmet",32),("finished/paw-commit",32),
              ("finished/helmet-commit-ack",32),("handshake/auth",32),("handshake/fragments",32)]
    material = {l:hkdf(root,th,"protogen/v4/session/"+l,n).hex() for l,n in labels}
    controls = {}
    for kind,key,role in [(21,"finished/paw",2),(22,"finished/helmet",1),
                          (23,"finished/paw-commit",2),(24,"finished/helmet-commit-ack",1)]:
        body = bytes([0x70,4,kind,role,2,255,0,0,40,0,40,0])+struct.pack("<Q",WIRE)
        body += (PAW if role==2 else HELMET)+b"\0\0"+th+bytes.fromhex(material["id"])+bytes(28)
        assert len(body)==96
        controls[str(kind)] = hmac.digest(bytes.fromhex(material[key]),body,"sha256").hex()
    return {"scope":"PUBLIC SYNTHETIC TEST FIXTURES; never production entropy or secrets",
            "inputs":"object[i][j]=(j+17*i)%256, ECDH=01..20, MLKEM=21..40; Session; wire=123456789abcdef0; identities 020000000003/020000000004",
            "transcript_bytes":len(transcript),"transcript_sha256":th.hex(),"hybrid_root":root.hex(),
            "material":material,"finished_commit_hmac":controls,
            "object_commitments":[hashlib.sha256(o).hexdigest() for o in objects]}

if __name__ == "__main__":
    value=build()
    if "--check" in sys.argv:
        if json.loads(PATH.read_text()) != value:raise SystemExit("v4 deterministic vector mismatch")
        print("Independent SHA3/SHA256/HKDF/Finished/Commit oracle passed")
    else:PATH.write_text(json.dumps(value,indent=2)+"\n")
