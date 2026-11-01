// Relógio de alta precisão. GetTickCount tem resolução de ~15,6 ms, o que dá
// dt=0 em muitos frames a 60+ fps; use isto para tudo que integra por frame.
#pragma once

#include <windows.h>

namespace sodcoop::clock {

inline double seconds() {
    static const double invFreq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return 1.0 / static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) * invFreq;
}

}  // namespace sodcoop::clock
