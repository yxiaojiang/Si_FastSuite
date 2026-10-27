#pragma once

#include "olp/cad/OcctCadKernel.h"

namespace olp::cad::detail {

core::TriMesh makeBoxMesh(const core::Vec3& bmin, const core::Vec3& bmax);

} // namespace olp::cad::detail
