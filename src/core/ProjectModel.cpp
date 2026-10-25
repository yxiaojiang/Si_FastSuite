#include "olp/core/ProjectModel.h"
#include <sstream>

namespace olp::core {

bool TopologicalSignature::matchesGeometry(const TopologicalSignature& other, double posTolMm) const noexcept {
    if ( std::abs(arcLengthMm - other.arcLengthMm) > posTolMm * 2.0 ) {
        return false;
    }
    const double dMid = (midPoint - other.midPoint).norm();
    if ( dMid > posTolMm ) {
        return false;
    }
    const bool directMatch =
        (startPoint - other.startPoint).norm() <= posTolMm &&
        (endPoint - other.endPoint).norm() <= posTolMm;
    const bool reversedMatch =
        (startPoint - other.endPoint).norm() <= posTolMm &&
        (endPoint - other.startPoint).norm() <= posTolMm;
    return directMatch || reversedMatch;
}

std::string FanucConfig::toString() const {
    std::ostringstream oss;
    oss << (flip ? 'F' : 'N') << ' '
        << (up   ? 'U' : 'D') << ' '
        << (top  ? 'T' : 'B') << ", "
        << turns[0] << ", " << turns[1] << ", " << turns[2];
    return oss.str();
}

} // namespace olp::core
