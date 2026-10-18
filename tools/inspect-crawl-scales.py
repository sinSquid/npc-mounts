"""Read-only donor validation; never repair invalid decoded transforms."""
import sys
from pathlib import Path
import numpy as np
sys.path.insert(0, sys.argv[1])
from anim_skyrim import load_skyrim_animation
root = Path(sys.argv[2])
for p in root.rglob('*.hkx'):
    try:
        a = load_skyrim_animation(str(p))
        v = np.array([x for t in a.tracks[:99] for x in t.scales])
        print(p.relative_to(root), a.num_tracks, 'scale', v.min(), v.max(), flush=True)
    except Exception as e:
        print(p.relative_to(root), type(e).__name__, str(e)[:80], flush=True)
