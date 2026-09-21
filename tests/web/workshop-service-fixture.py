#!/usr/bin/env python3
"""Private content API for browser acceptance; stdin EOF tears down all PHP workers."""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/p2p-signaling/test"))
from test_signaling import ServiceFixture

service = ServiceFixture()
try:
    service.write_config(allowed_origins=[sys.argv[1]], public_base_url=sys.argv[1])
    print(json.dumps({"port": service.port}), flush=True)
    sys.stdin.read()
finally:
    service.stop()
