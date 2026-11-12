"""Exact file admission for CPU-only accelerator replay, never GPU admission.

Keep complete SHA-256 identities: a matching instruction prefix alone is not
enough to execute a donor. Each replay still checks its own code/ABI fixtures.
The Tahoe identity was independently matched to the official Graphics 0.4.1
Tahoe payload. No Apple code or binary is distributed here.
"""
import hashlib

LEGACY = 'bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0'
TAHOE_041 = '567511bed855a5165cbc3615f61be7b4fcf607fde4fc5c48567fb127726d496e'


def require_accelerator(data, *, allow_forcewake_legacy=False):
    admitted = {LEGACY, TAHOE_041}
    if allow_forcewake_legacy:
        # Existing forcewake-only fixture; do not admit it to other replays.
        admitted.add('890735a93a19d3ddda24ff31bda3075dbd145fb7b26c11769f33c4564a8027ac')
    digest = hashlib.sha256(data).hexdigest()
    if digest not in admitted:
        raise ValueError('unsupported exact accelerator SHA-256: ' + digest)
    return digest
