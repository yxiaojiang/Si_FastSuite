#include "olp/ui/OcctViewport.h"

#ifdef OLP_HAS_OCCT
#include <AIS_InteractiveContext.hxx>
#include <AIS_Manipulator.hxx>
#include <AIS_ViewCube.hxx>
#include <V3d_View.hxx>
#endif

namespace olp::ui {

OcctViewport::OcctViewport(app::ProjectDocument& doc, app::CommandManager& cmdMgr)
    : doc_(doc), tcpGizmo_(doc, cmdMgr) {}

OcctViewport::~OcctViewport() = default;

void OcctViewport::fitAll() noexcept {
    // When linked with OpenCASCADE V3d_View, calls myView->FitAll(0.01, Standard_False)
}

} // namespace olp::ui
