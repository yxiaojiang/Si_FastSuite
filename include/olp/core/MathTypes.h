#pragma once

#include <array>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <string>

namespace olp::core {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;

inline double deg2rad(double deg) noexcept { return deg * kDegToRad; }
inline double rad2deg(double rad) noexcept { return rad * kRadToDeg; }

inline double normalizeAngleRad(double rad) noexcept {
    while (rad > kPi) rad -= 2.0 * kPi;
    while (rad <= -kPi) rad += 2.0 * kPi;
    return rad;
}

struct Vec3 {
    double x{0.0};
    double y{0.0};
    double z{0.0};

    constexpr Vec3() = default;
    constexpr Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& r) const noexcept { return {x + r.x, y + r.y, z + r.z}; }
    Vec3 operator-(const Vec3& r) const noexcept { return {x - r.x, y - r.y, z - r.z}; }
    Vec3 operator-() const noexcept { return {-x, -y, -z}; }
    Vec3 operator*(double s) const noexcept { return {x * s, y * s, z * s}; }
    Vec3 operator/(double s) const noexcept { return {x / s, y / s, z / s}; }

    double dot(const Vec3& r) const noexcept { return x * r.x + y * r.y + z * r.z; }
    Vec3 cross(const Vec3& r) const noexcept {
        return {
            y * r.z - z * r.y,
            z * r.x - x * r.z,
            x * r.y - y * r.x
        };
    }

    double normSq() const noexcept { return x * x + y * y + z * z; }
    double norm() const noexcept { return std::sqrt(normSq()); }

    Vec3 normalized(double eps = 1e-12) const noexcept {
        const double n = norm();
        return (n > eps) ? (*this / n) : Vec3{0.0, 0.0, 0.0};
    }
};

struct Mat3 {
    // Row-major 3x3 rotation matrix m[row][col]
    std::array<std::array<double, 3>, 3> m{{
        {{1.0, 0.0, 0.0}},
        {{0.0, 1.0, 0.0}},
        {{0.0, 0.0, 1.0}}
    }};

    static Mat3 identity() noexcept { return Mat3{}; }

    static Mat3 rotX(double rad) noexcept {
        const double c = std::cos(rad), s = std::sin(rad);
        Mat3 R;
        R.m[1][1] = c;  R.m[1][2] = -s;
        R.m[2][1] = s;  R.m[2][2] = c;
        return R;
    }

    static Mat3 rotY(double rad) noexcept {
        const double c = std::cos(rad), s = std::sin(rad);
        Mat3 R;
        R.m[0][0] = c;  R.m[0][2] = s;
        R.m[2][0] = -s; R.m[2][2] = c;
        return R;
    }

    static Mat3 rotZ(double rad) noexcept {
        const double c = std::cos(rad), s = std::sin(rad);
        Mat3 R;
        R.m[0][0] = c;  R.m[0][1] = -s;
        R.m[1][0] = s;  R.m[1][1] = c;
        return R;
    }

    static Mat3 fromColumns(const Vec3& colX, const Vec3& colY, const Vec3& colZ) noexcept {
        Mat3 R;
        R.m[0][0] = colX.x; R.m[0][1] = colY.x; R.m[0][2] = colZ.x;
        R.m[1][0] = colX.y; R.m[1][1] = colY.y; R.m[1][2] = colZ.y;
        R.m[2][0] = colX.z; R.m[2][1] = colY.z; R.m[2][2] = colZ.z;
        return R;
    }

    Vec3 colX() const noexcept { return {m[0][0], m[1][0], m[2][0]}; }
    Vec3 colY() const noexcept { return {m[0][1], m[1][1], m[2][1]}; }
    Vec3 colZ() const noexcept { return {m[0][2], m[1][2], m[2][2]}; }

    Mat3 transposed() const noexcept {
        Mat3 T;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                T.m[i][j] = m[j][i];
        return T;
    }

    Mat3 operator*(const Mat3& B) const noexcept {
        Mat3 C;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                C.m[i][j] = m[i][0] * B.m[0][j] + m[i][1] * B.m[1][j] + m[i][2] * B.m[2][j];
            }
        }
        return C;
    }

    Vec3 operator*(const Vec3& v) const noexcept {
        return {
            m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
            m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
            m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z
        };
    }

    // Angular distance between two rotation matrices in radians
    double angularDistanceRad(const Mat3& other) const noexcept {
        const Mat3 dR = transposed() * other;
        const double tr = dR.m[0][0] + dR.m[1][1] + dR.m[2][2];
        double cosTheta = (tr - 1.0) * 0.5;
        if (cosTheta > 1.0) cosTheta = 1.0;
        if (cosTheta < -1.0) cosTheta = -1.0;
        return std::acos(cosTheta);
    }

    // M09: Matrix determinant
    double determinant() const noexcept {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
             - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
             + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    }

    // M09: Gram-Schmidt orthonormalization preserving local Z axis as anchor
    static Mat3 orthonormalize(const Mat3& R) noexcept {
        Vec3 z = R.colZ().normalized();
        Vec3 x = R.colX();
        x = (x - z * x.dot(z)).normalized();
        Vec3 y = z.cross(x).normalized();
        return Mat3::fromColumns(x, y, z);
    }

    // M08: Spherical linear interpolation between two rotation matrices
    static Mat3 slerp(const Mat3& R1, const Mat3& R2, double t) noexcept;
};

struct Quat {
    double w{1.0};
    double x{0.0};
    double y{0.0};
    double z{0.0};

    static Quat fromMat3(const Mat3& R) noexcept {
        Quat q;
        const double trace = R.m[0][0] + R.m[1][1] + R.m[2][2];
        if (trace > 0.0) {
            const double s = 0.5 / std::sqrt(trace + 1.0);
            q.w = 0.25 / s;
            q.x = (R.m[2][1] - R.m[1][2]) * s;
            q.y = (R.m[0][2] - R.m[2][0]) * s;
            q.z = (R.m[1][0] - R.m[0][1]) * s;
        } else if (R.m[0][0] > R.m[1][1] && R.m[0][0] > R.m[2][2]) {
            const double s = 2.0 * std::sqrt(1.0 + R.m[0][0] - R.m[1][1] - R.m[2][2]);
            q.w = (R.m[2][1] - R.m[1][2]) / s;
            q.x = 0.25 * s;
            q.y = (R.m[0][1] + R.m[1][0]) / s;
            q.z = (R.m[0][2] + R.m[2][0]) / s;
        } else if (R.m[1][1] > R.m[2][2]) {
            const double s = 2.0 * std::sqrt(1.0 + R.m[1][1] - R.m[0][0] - R.m[2][2]);
            q.w = (R.m[0][2] - R.m[2][0]) / s;
            q.x = (R.m[0][1] + R.m[1][0]) / s;
            q.y = 0.25 * s;
            q.z = (R.m[1][2] + R.m[2][1]) / s;
        } else {
            const double s = 2.0 * std::sqrt(1.0 + R.m[2][2] - R.m[0][0] - R.m[1][1]);
            q.w = (R.m[1][0] - R.m[0][1]) / s;
            q.x = (R.m[0][2] + R.m[2][0]) / s;
            q.y = (R.m[1][2] + R.m[2][1]) / s;
            q.z = 0.25 * s;
        }
        return q.normalized();
    }

    Quat normalized() const noexcept {
        const double n = std::sqrt(w * w + x * x + y * y + z * z);
        if (n < 1e-12) return {1.0, 0.0, 0.0, 0.0};
        return {w / n, x / n, y / n, z / n};
    }

    Mat3 toMat3() const noexcept {
        const Quat q = normalized();
        Mat3 R;
        const double xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        const double xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        const double wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        R.m[0][0] = 1.0 - 2.0 * (yy + zz);
        R.m[0][1] = 2.0 * (xy - wz);
        R.m[0][2] = 2.0 * (xz + wy);
        R.m[1][0] = 2.0 * (xy + wz);
        R.m[1][1] = 1.0 - 2.0 * (xx + zz);
        R.m[1][2] = 2.0 * (yz - wx);
        R.m[2][0] = 2.0 * (xz - wy);
        R.m[2][1] = 2.0 * (yz + wx);
        R.m[2][2] = 1.0 - 2.0 * (xx + yy);
        return R;
    }

    static Quat slerp(const Quat& a, const Quat& b, double t) noexcept {
        Quat q1 = a.normalized();
        Quat q2 = b.normalized();
        double dot = q1.w * q2.w + q1.x * q2.x + q1.y * q2.y + q1.z * q2.z;
        if (dot < 0.0) {
            dot = -dot;
            q2 = {-q2.w, -q2.x, -q2.y, -q2.z};
        }
        if (dot > 0.9995) {
            return Quat{
                q1.w + t * (q2.w - q1.w),
                q1.x + t * (q2.x - q1.x),
                q1.y + t * (q2.y - q1.y),
                q1.z + t * (q2.z - q1.z)
            }.normalized();
        }
        const double theta0 = std::acos(dot);
        const double theta = theta0 * t;
        const double sinTheta = std::sin(theta);
        const double sinTheta0 = std::sin(theta0);
        const double s0 = std::cos(theta) - dot * sinTheta / sinTheta0;
        const double s1 = sinTheta / sinTheta0;
        return Quat{
            s0 * q1.w + s1 * q2.w,
            s0 * q1.x + s1 * q2.x,
            s0 * q1.y + s1 * q2.y,
            s0 * q1.z + s1 * q2.z
        }.normalized();
    }
};

inline Mat3 Mat3::slerp(const Mat3& R1, const Mat3& R2, double t) noexcept {
    const Quat q1 = Quat::fromMat3(R1);
    const Quat q2 = Quat::fromMat3(R2);
    return Quat::slerp(q1, q2, t).toMat3();
}

// FANUC Cartesian Pose (X, Y, Z in mm; W, P, R in degrees: R = Rz(R) * Ry(P) * Rx(W))
struct WprPose {
    double x{0.0};
    double y{0.0};
    double z{0.0};
    double wDeg{0.0};
    double pDeg{0.0};
    double rDeg{0.0};
};

// Homogeneous 4x4 Rigid Transform (translation in mm, rotation in Mat3)
struct Transform4d {
    Mat3 rot{};
    Vec3 pos{0.0, 0.0, 0.0};

    static Transform4d identity() noexcept { return Transform4d{}; }

    static Transform4d fromTranslation(double x, double y, double z) noexcept {
        Transform4d T;
        T.pos = {x, y, z};
        return T;
    }

    // Section 6.4: Construct from FANUC (X, Y, Z, W, P, R) where R = Rz(R) * Ry(P) * Rx(W)
    static Transform4d fromWPR(const WprPose& wpr) noexcept {
        Transform4d T;
        T.pos = {wpr.x, wpr.y, wpr.z};
        const double w = deg2rad(wpr.wDeg);
        const double p = deg2rad(wpr.pDeg);
        const double r = deg2rad(wpr.rDeg);
        T.rot = Mat3::rotZ(r) * Mat3::rotY(p) * Mat3::rotX(w);
        return T;
    }

    // Section 6.4: Extract FANUC (X, Y, Z, W, P, R) with gimbal-lock protection at P = +-90 deg
    WprPose toWPR() const noexcept {
        WprPose out;
        out.x = pos.x;
        out.y = pos.y;
        out.z = pos.z;

        const double r11 = rot.m[0][0], r12 = rot.m[0][1];
        const double r21 = rot.m[1][0], r22 = rot.m[1][1];
        const double r31 = rot.m[2][0], r32 = rot.m[2][1], r33 = rot.m[2][2];

        const double sy = std::sqrt(r11 * r11 + r21 * r21);
        const bool singular = (sy < 1e-7);

        double wRad = 0.0, pRad = 0.0, rRad = 0.0;
        pRad = std::atan2(-r31, sy);
        if (!singular) {
            wRad = std::atan2(r32, r33);
            rRad = std::atan2(r21, r11);
        } else {
            // Gimbal lock at P = +/- 90 deg: fix W = 0 and solve R
            wRad = 0.0;
            rRad = std::atan2(-r12, r22);
        }

        out.wDeg = rad2deg(wRad);
        out.pDeg = rad2deg(pRad);
        out.rDeg = rad2deg(rRad);
        return out;
    }

    // M04: Construct UFRAME from 3 picked points (origin, +X point, XY-plane point)
    static Transform4d fromThreePoints(const Vec3& origin, const Vec3& ptX, const Vec3& ptXY) noexcept {
        const Vec3 xAxis = (ptX - origin).normalized();
        const Vec3 tempY = (ptXY - origin).normalized();
        const Vec3 zAxis = xAxis.cross(tempY).normalized();
        const Vec3 yAxis = zAxis.cross(xAxis).normalized();
        Transform4d T;
        T.pos = origin;
        T.rot = Mat3::fromColumns(xAxis, yAxis, zAxis);
        return T;
    }

    Transform4d inverse() const noexcept {
        Transform4d inv;
        inv.rot = rot.transposed();
        inv.pos = -(inv.rot * pos);
        return inv;
    }

    Transform4d operator*(const Transform4d& rhs) const noexcept {
        Transform4d out;
        out.rot = rot * rhs.rot;
        out.pos = pos + (rot * rhs.pos);
        return out;
    }

    Vec3 transformPoint(const Vec3& p) const noexcept {
        return pos + (rot * p);
    }

    Vec3 transformVector(const Vec3& v) const noexcept {
        return rot * v;
    }

    static Transform4d interpolateSlerp(const Transform4d& a, const Transform4d& b, double t) noexcept {
        Transform4d out;
        out.pos = a.pos + (b.pos - a.pos) * t;
        out.rot = Quat::slerp(Quat::fromMat3(a.rot), Quat::fromMat3(b.rot), t).toMat3();
        return out;
    }
};

} // namespace olp::core
