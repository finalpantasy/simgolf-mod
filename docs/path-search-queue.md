# Complete path searches on expanded courses

Issue [95](https://github.com/finalpantasy/simgolf-mod/issues/95) supplied a
158x158 championship save with a crowd waiting at hole 11. A golfer in the
leading pair repeatedly walked near the same distant tiles while requesting
the tee as his destination. His partner and later pairs waited normally.

The original `pathStep` (`FUN_0042e7e0`) stored pending coordinates in two
1024-element arrays, with read and write cursors masked by `0x3ff`. The loop
treated equal cursors as empty without distinguishing a full queue. A trace
of the failing search recorded 18,207 dequeues and 19,230 additional enqueues:
including the initial entry, 1024 entries remained when the cursors met and
the search stopped. The golfer's tile had cost zero (unreached). A nearby
search completed with a cost of 66 and sent him back, producing the loop.

The queue now counts pending entries explicitly and grows transactionally
when full. It retains exact FIFO relaxation order, including repeated tile
entries. Costs, tie breaking, search limits, destinations, fatigue and tee
priority are unchanged. Ordinary searches use static storage; larger searches
reuse their peak allocation. If allocation fails, the search uses its existing
no-route direction fallback and retries normally on a later search. The
translator requires all four replacement anchors exactly once.

Run `bash port/tests/path_queue.sh`. The standalone C test covers the original
full-ring ambiguity, wrapped growth, FIFO preservation, allocation failure
and retry, and 100,000 operations compared with the original ring below its
capacity. The Python test ensures missing or duplicated translator anchors
stop the build. Full repository verification remains `bash tools/check.sh`.

Native acceptance uses a private, disposable copy of the supplied save:
observe the leading pair resume play without relocating actors or changing
simulation settings. Save files, companion data and diagnostic captures are
not included in this repository. The correction does not claim to resolve
the separately reported global championship freezes in issue 69.
