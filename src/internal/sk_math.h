#ifndef SK_INTERNAL_MATH_H
#define SK_INTERNAL_MATH_H

#include <math.h>

#include "sk_types.h"

#define SK_DEG2RAD 0.01745329251994329577f /* degrees -> radians (pi / 180) */
#define SK_RAD2DEG 57.2957795130823208768f /* radians -> degrees (180 / pi) */

/* Column-major 4x4 matrices (OpenGL convention), matching sokol_gl's
 * perspective/lookat so custom-pipeline meshes line up with sokol_gl shapes. */
typedef struct {
    float m[16];
} sk_mat4_t;

static inline sk_mat4_t sk_mat4_identity(void)
{
    sk_mat4_t r = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    return r;
}

static inline sk_mat4_t sk_mat4_mul(sk_mat4_t a, sk_mat4_t b)
{
    sk_mat4_t r;
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            r.m[col * 4 + row] = a.m[0 * 4 + row] * b.m[col * 4 + 0] +
                                 a.m[1 * 4 + row] * b.m[col * 4 + 1] +
                                 a.m[2 * 4 + row] * b.m[col * 4 + 2] +
                                 a.m[3 * 4 + row] * b.m[col * 4 + 3];
        }
    }
    return r;
}

static inline sk_mat4_t sk_mat4_perspective(float fovy_rad, float aspect, float n, float f)
{
    sk_mat4_t r = {{0}};
    float t = tanf(fovy_rad * 0.5f);
    r.m[0] = 1.0f / (aspect * t);
    r.m[5] = 1.0f / t;
    r.m[10] = (f + n) / (n - f);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * f * n) / (n - f);
    return r;
}

/* glOrtho / sgl_ortho convention (column-major). */
static inline sk_mat4_t sk_mat4_ortho(float l, float r, float b, float t, float n, float f)
{
    sk_mat4_t m = {{0}};
    m.m[0] = 2.0f / (r - l);
    m.m[5] = 2.0f / (t - b);
    m.m[10] = -2.0f / (f - n);
    m.m[12] = -(r + l) / (r - l);
    m.m[13] = -(t + b) / (t - b);
    m.m[14] = -(f + n) / (f - n);
    m.m[15] = 1.0f;
    return m;
}

static inline vec3_t sk_v3_sub(vec3_t a, vec3_t b) { return (vec3_t){a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline vec3_t sk_v3_cross(vec3_t a, vec3_t b)
{
    return (vec3_t){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static inline vec3_t sk_v3_norm(vec3_t a)
{
    float l = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
    if (l <= 1e-6f) return (vec3_t){0, 0, 0};
    return (vec3_t){a.x / l, a.y / l, a.z / l};
}

static inline sk_mat4_t sk_mat4_lookat(vec3_t eye, vec3_t center, vec3_t up)
{
    vec3_t f = sk_v3_norm(sk_v3_sub(center, eye));
    vec3_t s = sk_v3_norm(sk_v3_cross(f, up));
    vec3_t u = sk_v3_cross(s, f);
    sk_mat4_t r = sk_mat4_identity();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -(s.x * eye.x + s.y * eye.y + s.z * eye.z);
    r.m[13] = -(u.x * eye.x + u.y * eye.y + u.z * eye.z);
    r.m[14] = (f.x * eye.x + f.y * eye.y + f.z * eye.z);
    return r;
}

static inline sk_mat4_t sk_mat4_translate(float x, float y, float z)
{
    sk_mat4_t r = sk_mat4_identity();
    r.m[12] = x; r.m[13] = y; r.m[14] = z;
    return r;
}

static inline sk_mat4_t sk_mat4_scale(float x, float y, float z)
{
    sk_mat4_t r = sk_mat4_identity();
    r.m[0] = x; r.m[5] = y; r.m[10] = z;
    return r;
}

static inline sk_mat4_t sk_mat4_rotate(float angle_rad, float x, float y, float z)
{
    float len = sqrtf(x * x + y * y + z * z);
    sk_mat4_t r = sk_mat4_identity();
    float c, s, t;
    if (len <= 1e-6f) return r;
    x /= len; y /= len; z /= len;
    c = cosf(angle_rad);
    s = sinf(angle_rad);
    t = 1.0f - c;
    r.m[0] = t * x * x + c;     r.m[1] = t * x * y + s * z; r.m[2] = t * x * z - s * y;
    r.m[4] = t * x * y - s * z; r.m[5] = t * y * y + c;     r.m[6] = t * y * z + s * x;
    r.m[8] = t * x * z + s * y; r.m[9] = t * y * z - s * x; r.m[10] = t * z * z + c;
    return r;
}

static inline sk_mat4_t sk_mat4_from_quat(quat_t q)
{
    sk_mat4_t r = sk_mat4_identity();
    float x = q.x, y = q.y, z = q.z, w = q.w;
    float xx = x * x, yy = y * y, zz = z * z;
    float xy = x * y, xz = x * z, yz = y * z;
    float wx = w * x, wy = w * y, wz = w * z;
    r.m[0] = 1.0f - 2.0f * (yy + zz);
    r.m[1] = 2.0f * (xy + wz);
    r.m[2] = 2.0f * (xz - wy);
    r.m[4] = 2.0f * (xy - wz);
    r.m[5] = 1.0f - 2.0f * (xx + zz);
    r.m[6] = 2.0f * (yz + wx);
    r.m[8] = 2.0f * (xz + wy);
    r.m[9] = 2.0f * (yz - wx);
    r.m[10] = 1.0f - 2.0f * (xx + yy);
    return r;
}

/* Compose from translation + quaternion rotation + scale (T * R * S). */
static inline sk_mat4_t sk_mat4_compose(vec3_t t, quat_t q, vec3_t s)
{
    sk_mat4_t m = sk_mat4_from_quat(q);
    /* scale columns */
    m.m[0] *= s.x; m.m[1] *= s.x; m.m[2] *= s.x;
    m.m[4] *= s.y; m.m[5] *= s.y; m.m[6] *= s.y;
    m.m[8] *= s.z; m.m[9] *= s.z; m.m[10] *= s.z;
    m.m[12] = t.x; m.m[13] = t.y; m.m[14] = t.z;
    return m;
}

static inline vec3_t sk_v3_lerp(vec3_t a, vec3_t b, float t)
{
    return (vec3_t){a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

static inline quat_t sk_quat_slerp(quat_t a, quat_t b, float t)
{
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    float k0, k1, theta, sin_theta;
    quat_t r;
    if (dot < 0.0f) { /* shortest path */
        b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w; dot = -dot;
    }
    if (dot > 0.9995f) { /* nearly parallel: lerp + normalize */
        r.x = a.x + (b.x - a.x) * t; r.y = a.y + (b.y - a.y) * t;
        r.z = a.z + (b.z - a.z) * t; r.w = a.w + (b.w - a.w) * t;
    } else {
        theta = acosf(dot);
        sin_theta = sinf(theta);
        k0 = sinf((1.0f - t) * theta) / sin_theta;
        k1 = sinf(t * theta) / sin_theta;
        r.x = a.x * k0 + b.x * k1; r.y = a.y * k0 + b.y * k1;
        r.z = a.z * k0 + b.z * k1; r.w = a.w * k0 + b.w * k1;
    }
    {
        float len = sqrtf(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
        if (len > 1e-6f) { r.x /= len; r.y /= len; r.z /= len; r.w /= len; }
    }
    return r;
}

/* Transform a point (w=1, perspective divide) by a column-major matrix. */
static inline vec3_t sk_mat4_mul_point(sk_mat4_t m, vec3_t p)
{
    float x = m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12];
    float y = m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13];
    float z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
    float w = m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15];
    if (w > 1e-8f || w < -1e-8f) { x /= w; y /= w; z /= w; }
    return (vec3_t){x, y, z};
}

/* Full 4x4 inverse (returns identity if singular). */
static inline sk_mat4_t sk_mat4_inverse(sk_mat4_t a)
{
    const float *m = a.m;
    float inv[16], det;
    sk_mat4_t r;

    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];

    det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (det > -1e-12f && det < 1e-12f) return sk_mat4_identity();
    det = 1.0f / det;
    for (int i = 0; i < 16; i++) r.m[i] = inv[i] * det;
    return r;
}

/* Compose a transform: T * Rz * Ry * Rx * S (euler radians). */
static inline sk_mat4_t sk_mat4_trs(vec3_t pos, vec3_t rot, vec3_t scale)
{
    sk_mat4_t m = sk_mat4_translate(pos.x, pos.y, pos.z);
    m = sk_mat4_mul(m, sk_mat4_rotate(rot.z, 0, 0, 1));
    m = sk_mat4_mul(m, sk_mat4_rotate(rot.y, 0, 1, 0));
    m = sk_mat4_mul(m, sk_mat4_rotate(rot.x, 1, 0, 0));
    m = sk_mat4_mul(m, sk_mat4_scale(scale.x, scale.y, scale.z));
    return m;
}

#endif // SK_INTERNAL_MATH_H
