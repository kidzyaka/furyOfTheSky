#include "fx/water_marks.h"

namespace fx {

void WaterMarks::add(const WaterMark& m) {
    if (n_ < kMax) {
        m_[n_++] = m;
        return;
    }
    // Replace the one nearest its end.
    int oldest = 0;
    for (int i = 1; i < n_; ++i)
        if (m_[i].t() > m_[oldest].t()) oldest = i;
    m_[oldest] = m;
}

void WaterMarks::update(float dt) {
    for (int i = 0; i < n_;) {
        WaterMark& m = m_[i];
        if ((m.age += dt) >= m.life) {
            m = m_[--n_];
            continue;
        }
        ++i;
    }
}

}  // namespace fx
