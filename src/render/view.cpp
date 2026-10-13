#include "render/view.h"

#include <pspgu.h>
#include <pspgum.h>

#include "render/gu_context.h"

namespace gfx {

void setProjection(float fovDeg, float nearZ, float farZ) {
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(fovDeg, static_cast<float>(kScreenW) / kScreenH, nearZ, farZ);
}

void setView(const math::Vec3& eye, const math::Vec3& target, const math::Vec3& up) {
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    ScePspFVector3 e = {eye.x, eye.y, eye.z};
    ScePspFVector3 t = {target.x, target.y, target.z};
    ScePspFVector3 u = {up.x, up.y, up.z};
    sceGumLookAt(&e, &t, &u);
}

void setModelIdentity() {
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
}

void setModelTranslation(const math::Vec3& pos) {
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    ScePspFVector3 p = {pos.x, pos.y, pos.z};
    sceGumTranslate(&p);
}

void setModel(const math::Vec3& pos, const math::Quat& rot) {
    // pspgum matrices are column-major: x/y/z/w are the images of the basis
    // vectors and the translation (see gumTranslate writing into .w).
    math::Vec3 r = rot.rotate(math::kRight), u = rot.rotate(math::kUp), b = rot.rotate({0, 0, 1});
    ScePspFMatrix4 m = {{r.x, r.y, r.z, 0.0f}, {u.x, u.y, u.z, 0.0f}, {b.x, b.y, b.z, 0.0f},
                        {pos.x, pos.y, pos.z, 1.0f}};
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadMatrix(&m);
}

void enableSunLight(const math::Vec3& dirToSun) {
    // World-space light (docs/psp/gotchas.md #7); for GU_DIRECTIONAL the
    // "position" is the direction towards the light.
    ScePspFVector3 sun = {dirToSun.x, dirToSun.y, dirToSun.z};
    sceGuEnable(GU_LIGHTING);
    sceGuEnable(GU_LIGHT0);
    sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE_AND_SPECULAR, &sun);
    sceGuLightColor(0, GU_DIFFUSE, abgr(255, 244, 228));
    sceGuLightColor(0, GU_SPECULAR, abgr(112, 112, 112));
    sceGuLightMode(GU_SEPARATE_SPECULAR_COLOR);
    sceGuSpecular(14.0f);
    sceGuAmbient(abgr(82, 92, 106));
    sceGuColor(0xFFFFFFFF);
}

void disableLighting() {
    sceGuDisable(GU_LIGHTING);
    sceGuColor(0xFFFFFFFF);
}

void enableFog(float nearZ, float farZ, uint32_t color) {
    sceGuFog(nearZ, farZ, color);
    sceGuEnable(GU_FOG);
}

void disableFog() { sceGuDisable(GU_FOG); }

}  // namespace gfx
