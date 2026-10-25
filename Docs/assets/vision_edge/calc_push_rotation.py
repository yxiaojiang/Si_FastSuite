"""平放木板在托台上受水平接触力时的滑移临界力（平动 + 绕瞬心转动）。

假设：板重均匀压在托台支撑区上（库仑摩擦，各点压力相同）；接触力水平、垂直于板边。
输出 F_slip / (mu*G)：1.0 表示与纯平动推板相同，越小越容易被“拧转”。
用法：python calc_push_rotation.py
"""
import numpy as np


def support(kind, size, n=60):
    g = np.linspace(-size / 2, size / 2, n)
    X, Y = np.meshgrid(g, g)
    pts = np.c_[X.ravel(), Y.ravel()]
    if kind == "disc":
        pts = pts[np.hypot(pts[:, 0], pts[:, 1]) <= size / 2]
    w = np.full(len(pts), 1.0 / len(pts))
    return pts, w


def wrench(pts, w, c, s):
    r = pts - c
    v = s * np.c_[-r[:, 1], r[:, 0]]
    nv = np.linalg.norm(v, axis=1, keepdims=True) + 1e-12
    f = -w[:, None] * v / nv
    F = f.sum(0)
    M = (pts[:, 0] * f[:, 1] - pts[:, 1] * f[:, 0]).sum()
    return np.array([F[0], F[1], M])


def limit_force(pts, w, P, d, L=100.0):
    u = np.array([d[0], d[1], P[0] * d[1] - P[1] * d[0]])
    target = -u / np.array([1, 1, L])
    target /= np.linalg.norm(target)

    def score(c, s):
        W = wrench(pts, w, c, s) / np.array([1, 1, L])
        return 1 - W @ target / np.linalg.norm(W)

    rng = np.concatenate([-np.logspace(0, 5, 60)[::-1], [0], np.logspace(0, 5, 60)])
    best, arg = 1e9, None
    for s in (1, -1):
        for cx in rng:
            for cy in rng:
                sc = score(np.array([cx, cy]), s)
                if sc < best:
                    best, arg = sc, (np.array([cx, cy]), s)
    c, s = arg
    step = max(abs(c[0]), abs(c[1]), 10) * 0.2
    while step > 1e-4:
        moved = False
        for dc in ([step, 0], [-step, 0], [0, step], [0, -step]):
            sc = score(c + dc, s)
            if sc < best:
                best, c, moved = sc, c + dc, True
        if not moved:
            step *= 0.6
    W = wrench(pts, w, c, s)
    return np.hypot(W[0], W[1]), best, c


CASES = [
    ("200 板 / 170 方满托 / 推边中点", "square", 170, (100, 0)),
    ("200 板 / 170 方满托 / 推边端头", "square", 170, (100, 100)),
    ("200 板 / Ø70 圆托 / 推边中点", "disc", 70, (100, 0)),
    ("200 板 / Ø70 圆托 / 推边端头", "disc", 70, (100, 100)),
    ("200 板 / Ø50 圆托 / 推边端头", "disc", 50, (100, 100)),
    ("300 板 / Ø70 圆托 / 推边端头", "disc", 70, (150, 150)),
]

if __name__ == "__main__":
    for name, kind, size, P in CASES:
        pts, w = support(kind, size)
        F, err, c = limit_force(pts, w, np.array(P, float), np.array([-1.0, 0.0]))
        print(f"{name}: F_slip/(mu*G) = {F:.3f}   residual={err:.1e}  ICR={c.round(1)}")
