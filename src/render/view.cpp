#include "render/view.h"

#include <pspgu.h>
#include <pspgum.h>

#include <algorithm>
#include <cmath>

#include "render/gu_context.h"

namespace gfx {
namespace {
math::Vec3 g_origin;  // camera position: the render-space origin
CameraBasis g_camera;
float g_fogNear = 1.0f, g_fogFar = 2.0f;
uint32_t g_fogColor = 0;
}  // namespace

void setProjection(float fovDeg, float nearZ, float farZ) {
    const float aspect = static_cast<float>(kScreenW) / kScreenH;
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(fovDeg, aspect, nearZ, farZ);  // fov is vertical
    g_camera.tanHalfY = std::tan(math::radians(fovDeg) * 0.5f);
    g_camera.tanHalfX = g_camera.tanHalfY * aspect;
}

const CameraBasis& camera() { return g_camera; }

bool projectDirection(const math::Vec3& dir, float& sx, float& sy) {
    float z = math::dot(dir, g_camera.forward);
    float zs = std::fabs(z) > 1e-4f ? std::fabs(z) : 1e-4f;
    sx = kScreenW * 0.5f * (1.0f + math::dot(dir, g_camera.right) / (zs * g_camera.tanHalfX));
    sy = kScreenH * 0.5f * (1.0f - math::dot(dir, g_camera.up) / (zs * g_camera.tanHalfY));
    return z > 1e-4f;
}

bool sphereVisible(const math::Vec3& centre, float radius, float maxDistance) {
    const math::Vec3 rel = centre - g_origin;
    const float z = math::dot(rel, g_camera.forward);
    if (z < -radius || z > maxDistance + radius) return false;
    const float zc = std::max(z, 1e-3f);
    // Half-extent of the view at depth z, padded by the radius (slant-safe).
    const float x = std::fabs(math::dot(rel, g_camera.right)), y = std::fabs(math::dot(rel, g_camera.up));
    return x <= zc * g_camera.tanHalfX + radius * 1.5f && y <= zc * g_camera.tanHalfY + radius * 1.5f;
}

bool projectPoint(const math::Vec3& world, float& sx, float& sy) {
    return projectDirection(world - g_origin, sx, sy);
}

void setCamera(const math::Vec3& eye, const math::Vec3& target, const math::Vec3& up) {
    g_origin = eye;
    g_camera.eye = eye;
    g_camera.forward = math::normalize(target - eye);
    g_camera.right = math::normalize(math::cross(g_camera.forward, up));
    g_camera.up = math::cross(g_camera.right, g_camera.forward);
    math::Vec3 t = target - eye;  // full float precision, on the CPU
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    ScePspFVector3 e = {0.0f, 0.0f, 0.0f};
    ScePspFVector3 tv = {t.x, t.y, t.z};
    ScePspFVector3 u = {up.x, up.y, up.z};
    sceGumLookAt(&e, &tv, &u);
}

const math::Vec3& renderOrigin() { return g_origin; }

void setModelRenderSpace() {
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
}

void setModelTranslation(const math::Vec3& worldPos) {
    math::Vec3 p = worldPos - g_origin;
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    ScePspFVector3 v = {p.x, p.y, p.z};
    sceGumTranslate(&v);
}

void setModelTranslationScale(const math::Vec3& worldPos, float sx, float sy, float sz) {
    const math::Vec3 p = worldPos - g_origin;
    ScePspFMatrix4 m = {{sx, 0.0f, 0.0f, 0.0f}, {0.0f, sy, 0.0f, 0.0f}, {0.0f, 0.0f, sz, 0.0f}, {p.x, p.y, p.z, 1.0f}};
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadMatrix(&m);
}

void setModel(const math::Vec3& worldPos, const math::Quat& rot) {
    // pspgum matrices are column-major: x/y/z/w are the images of the basis
    // vectors and the translation (see gumTranslate writing into .w).
    math::Vec3 pos = worldPos - g_origin;
    math::Vec3 r = rot.rotate(math::kRight), u = rot.rotate(math::kUp), b = rot.rotate({0, 0, 1});
    ScePspFMatrix4 m = {{r.x, r.y, r.z, 0.0f}, {u.x, u.y, u.z, 0.0f}, {b.x, b.y, b.z, 0.0f},
                        {pos.x, pos.y, pos.z, 1.0f}};
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadMatrix(&m);
}

void setModel(const math::Vec3& worldPos, const math::Quat& rot, const math::Vec3& scale) {
    const math::Vec3 pos = worldPos - g_origin;
    const math::Vec3 r = rot.rotate(math::kRight) * scale.x, u = rot.rotate(math::kUp) * scale.y,
                     b = rot.rotate({0, 0, 1}) * scale.z;
    ScePspFMatrix4 m = {{r.x, r.y, r.z, 0.0f}, {u.x, u.y, u.z, 0.0f}, {b.x, b.y, b.z, 0.0f},
                        {pos.x, pos.y, pos.z, 1.0f}};
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadMatrix(&m);
}

void enableSunLight(const math::Vec3& dirToSun) {
    // World-space lights (docs/psp/gotchas.md #7) - render space is world
    // shifted by the eye, which leaves directions untouched. For
    // GU_DIRECTIONAL the "position" is the direction towards the light.
    ScePspFVector3 sun = {dirToSun.x, dirToSun.y, dirToSun.z};
    ScePspFVector3 sky = {0.0f, 1.0f, 0.0f};
    ScePspFVector3 sea = {0.0f, -1.0f, 0.0f};
    sceGuEnable(GU_LIGHTING);
    sceGuEnable(GU_LIGHT0);
    sceGuEnable(GU_LIGHT1);
    sceGuEnable(GU_LIGHT2);
    sceGuDisable(GU_LIGHT3);
    sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE_AND_SPECULAR, &sun);
    sceGuLightColor(0, GU_DIFFUSE, abgr(255, 242, 222));
    sceGuLightColor(0, GU_SPECULAR, abgr(150, 150, 146));
    // Hemisphere approximation: upper surfaces pick up the blue sky, lower
    // ones the darker sea - instead of one flat ambient everywhere.
    sceGuLight(1, GU_DIRECTIONAL, GU_DIFFUSE, &sky);
    sceGuLightColor(1, GU_DIFFUSE, abgr(58, 74, 100));
    sceGuLight(2, GU_DIRECTIONAL, GU_DIFFUSE, &sea);
    sceGuLightColor(2, GU_DIFFUSE, abgr(22, 42, 54));
    sceGuLightMode(GU_SEPARATE_SPECULAR_COLOR);
    sceGuSpecular(16.0f);
    sceGuAmbient(abgr(54, 60, 70));
    sceGuColor(0xFFFFFFFF);
}

void setPointLight(const math::Vec3& worldPos, uint32_t color, float strength) {
    if (strength <= 0.01f) {
        sceGuDisable(GU_LIGHT3);
        return;
    }
    math::Vec3 p = worldPos - g_origin;  // render space
    ScePspFVector3 pos = {p.x, p.y, p.z};
    sceGuLight(3, GU_POINTLIGHT, GU_DIFFUSE, &pos);
    sceGuLightColor(3, GU_DIFFUSE, color);
    // 1 / (a0 + a1 d + a2 d^2): the tail booms (~3 m) get most of it.
    sceGuLightAtt(3, 0.6f / strength, 0.0f, 0.12f / strength);
    sceGuEnable(GU_LIGHT3);
}

void enableDistantLight(const math::Vec3& dirToSun, uint32_t key, uint32_t ambient) {
    ScePspFVector3 sun = {dirToSun.x, dirToSun.y, dirToSun.z};
    sceGuEnable(GU_LIGHTING);
    sceGuEnable(GU_LIGHT0);
    sceGuDisable(GU_LIGHT1);
    sceGuDisable(GU_LIGHT2);
    sceGuDisable(GU_LIGHT3);
    sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE, &sun);
    sceGuLightColor(0, GU_DIFFUSE, key);
    sceGuLightMode(GU_SINGLE_COLOR);
    // The sky and sea fill folded into a brighter ambient.
    sceGuAmbient(ambient);
    sceGuColor(0xFFFFFFFF);
}

void enableLightRig(const LightRig& rig) {
    ScePspFVector3 key = {rig.keyDir.x, rig.keyDir.y, rig.keyDir.z};
    ScePspFVector3 fill = {rig.fillDir.x, rig.fillDir.y, rig.fillDir.z};
    ScePspFVector3 bounce = {rig.bounceDir.x, rig.bounceDir.y, rig.bounceDir.z};
    sceGuEnable(GU_LIGHTING);
    sceGuEnable(GU_LIGHT0);
    sceGuEnable(GU_LIGHT1);
    sceGuEnable(GU_LIGHT2);
    sceGuDisable(GU_LIGHT3);
    sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE_AND_SPECULAR, &key);
    sceGuLightColor(0, GU_DIFFUSE, rig.key);
    sceGuLightColor(0, GU_SPECULAR, rig.keySpecular);
    sceGuLight(1, GU_DIRECTIONAL, GU_DIFFUSE, &fill);
    sceGuLightColor(1, GU_DIFFUSE, rig.fill);
    // The GE lights every vertex per enabled light: a black bounce costs as much as a real one.
    if (rig.bounce & 0x00FFFFFFu) {
        sceGuLight(2, GU_DIRECTIONAL, GU_DIFFUSE, &bounce);
        sceGuLightColor(2, GU_DIFFUSE, rig.bounce);
    } else {
        sceGuDisable(GU_LIGHT2);
    }
    sceGuLightMode(GU_SEPARATE_SPECULAR_COLOR);
    sceGuSpecular(12.0f);
    sceGuAmbient(rig.ambient);
    sceGuColor(0xFFFFFFFF);
}

void disableLighting() {
    sceGuDisable(GU_LIGHTING);
    sceGuColor(0xFFFFFFFF);
}

void enableFog(float nearZ, float farZ, uint32_t color) {
    g_fogNear = nearZ;
    g_fogFar = farZ;
    g_fogColor = color;
    sceGuFog(nearZ, farZ, color);
    sceGuEnable(GU_FOG);
}

void currentFog(float& nearZ, float& farZ, uint32_t& color) {
    nearZ = g_fogNear;
    farZ = g_fogFar;
    color = g_fogColor;
}

void setAdditiveFog(bool additive) { sceGuFog(g_fogNear, g_fogFar, additive ? 0u : g_fogColor); }

void disableFog() { sceGuDisable(GU_FOG); }

}  // namespace gfx
