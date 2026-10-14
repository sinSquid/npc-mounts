#include "../src/Motion.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main()
{
    int gait = 0;
    assert(Motion::Choose(0, 0, 0, gait) == 0);
    assert(Motion::Choose(120, 1, 0, gait) == 1);
    assert(Motion::Choose(181, 1, 0, gait) == 2);
    for (int i=0; i<10000; ++i)
        assert(Motion::Choose(i % 2 ? 179 : 181, 1, 0, gait) == 2);
    assert(Motion::Choose(159, 1, 0, gait) == 1);
    assert(Motion::Choose(451, 1, 0, gait) == 3);
    for (int i=0; i<10000; ++i)
        assert(Motion::Choose(i % 2 ? 449 : 451, 1, 0, gait) == 3);
    assert(Motion::Choose(419, 1, 0, gait) == 2);
    assert(Motion::Choose(200, -1, 0, gait) == 4);
    assert(Motion::Choose(0, -1, 0, gait) == 0);
    for (float fps : {30.f, 60.f, 144.f}) {
        float dt = 1.f / fps;
        assert(Motion::Choose(0, 0, (0.7f * dt) / dt, gait) == 6);
        assert(Motion::Choose(0, 0, (-0.7f * dt) / dt, gait) == 5);
        assert(Motion::Choose(0, 0, (0.5f * dt) / dt, gait) == 0);
    }
    std::puts("PASS: 20000 walk/run/sprint threshold samples, backward motion, 30/60/144 FPS turn equivalence.");
}
