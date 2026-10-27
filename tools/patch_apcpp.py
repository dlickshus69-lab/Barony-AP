"""Make APCpp's server-message queue thread-safe.

APCpp adds messages from its network thread and the game reads them on the main thread,
with no lock. An item or hint message arriving at the wrong moment could crash the game.
Run from the workflow after cloning APCpp: python tools/patch_apcpp.py extern/APCpp
"""
import re
import sys
from pathlib import Path

path = Path(sys.argv[1]) / "Archipelago.cpp"
src = path.read_text(encoding="utf-8")
if "messageQueueMutex" in src:
    sys.exit(0)

src = src.replace("std::deque<AP_Message*> messageQueue;",
                  "std::deque<AP_Message*> messageQueue;\nstd::recursive_mutex messageQueueMutex;", 1)
if "#include <mutex>" not in src:
    src = "#include <mutex>\n" + src

count = src.count("messageQueue.push_back(")
src = re.sub(r"messageQueue\.push_back\(([^;]*)\);",
             r"{ std::lock_guard<std::recursive_mutex> apLock(messageQueueMutex); messageQueue.push_back(\1); }", src)

for sig in ("bool AP_IsMessagePending() {", "AP_Message* AP_GetLatestMessage() {", "void AP_ClearLatestMessage() {"):
    assert sig in src, sig
    src = src.replace(sig, sig + "\n    std::lock_guard<std::recursive_mutex> apLock(messageQueueMutex);", 1)

path.write_text(src, encoding="utf-8")
print(f"APCpp: message queue locked ({count} producers)")
