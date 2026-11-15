#pragma once
// Pi as a constant: M_PI is not standard C++ and MSVC only defines it if _USE_MATH_DEFINES comes before the
// first <cmath> anywhere in the translation unit, which a header cannot guarantee. Same value as M_PI.
namespace dspmath { constexpr double kPi = 3.14159265358979323846; }
