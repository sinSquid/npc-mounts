"""Observe the existing decoder without editing its files or source animations."""
import sys

sys.path.insert(0, sys.argv[1])
import anim_fo4
import anim_skyrim

original = anim_fo4._eval_bspline
calls = 0


def observe(span, degree, time, knots, control_points):
    global calls
    value = original(span, degree, time, knots, control_points)
    if calls < 68 and control_points and isinstance(control_points[0], (int, float)):
        print(calls, 'span', span, 'degree', degree, 'time', time,
              'control range', min(control_points), max(control_points),
              'sample', value, 'partition',
              original(span, degree, time, knots, [1.0]*len(control_points)))
    calls += 1
    return value


anim_fo4._eval_bspline = observe
anim_skyrim.load_skyrim_animation(sys.argv[2])
