#include "MathVect.h"

#include <cmath>

MathVect2::MathVect2(double dx, double dy): dx(dx), dy(dy) {}

double MathVect2::scalarProduct(const MathVect2& u, const MathVect2& v) { return u.dx * v.dx + u.dy * v.dy; }
double MathVect2::norm() const { return std::hypot(dx, dy); }
double MathVect2::argument() const { return std::atan2(dy, dx); }

MathVect2 MathVect2::operator+(const MathVect2& u) const { return {dx + u.dx, dy + u.dy}; }
MathVect2 MathVect2::operator-(const MathVect2& u) const { return {dx - u.dx, dy - u.dy}; }
MathVect2 MathVect2::operator-() const { return {-dx, -dy}; }

MathVect2 operator*(const double c, const MathVect2& u) { return {c * u.dx, c * u.dy}; }
