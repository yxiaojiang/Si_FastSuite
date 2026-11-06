#include "olp/runtime/ContourDataset.h"
#include "olp/io/Utf8Path.h"

#include <algorithm>
#include <filesystem>
#include <cctype>
#include <fstream>
#include <sstream>

#if defined(OLP_HAS_NLOHMANN_JSON)
#include <nlohmann/json.hpp>
#endif

namespace olp::runtime {
namespace {

bool endsWithIgnoreCase(const std::string& value, const std::string& suffix) {
    if (value.size() < suffix.size()) {
        return false;
    }
    for (size_t i = 0; i < suffix.size(); ++i) {
        const char a = static_cast<char>(std::tolower(
            static_cast<unsigned char>(value[value.size() - suffix.size() + i])));
        const char b = static_cast<char>(std::tolower(
            static_cast<unsigned char>(suffix[i])));
        if (a != b) {
            return false;
        }
    }
    return true;
}

std::string stemFromFilename(const std::string& filename) {
    const auto dot = filename.find_last_of('.');
    if (dot == std::string::npos) {
        return filename;
    }
    return filename.substr(0, dot);
}

DeviceResult<ContourFrame> loadCsvFrame(
    const std::string& pathUtf8,
    const std::string& placementId) {
    std::ifstream in(olp::io::utf8ToPath(pathUtf8));
    if (!in) {
        return DeviceResult<ContourFrame>::failure(
            DeviceError::IoFailure,
            "Failed to open contour CSV: " + pathUtf8);
    }

    ContourFrame frame;
    frame.placementId = placementId;
    frame.frameId = "camera";
    frame.boardThicknessMm = 18.0;
    frame.fakePointCount = 0;

    std::string line;
    bool headerSkipped = false;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        if (!headerSkipped) {
            headerSkipped = true;
            // Allow optional header row "x_mm,y_mm,z_mm"
            const bool looksLikeHeader =
                line.find_first_of("XxYyZz") != std::string::npos &&
                line.find_first_of("0123456789") == std::string::npos;
            if (looksLikeHeader) {
                continue;
            }
        }

        for (char& ch : line) {
            if (ch == ';') {
                ch = ',';
            }
        }
        std::stringstream ss(line);
        std::string sx;
        std::string sy;
        std::string sz;
        if (!std::getline(ss, sx, ',') || !std::getline(ss, sy, ',')) {
            return DeviceResult<ContourFrame>::failure(
                DeviceError::InvalidArgument,
                "Malformed CSV row in " + pathUtf8 + ": " + line);
        }
        if (!std::getline(ss, sz, ',')) {
            sz = "0";
        }
        try {
            ContourPointMm pt;
            pt.x = std::stod(sx);
            pt.y = std::stod(sy);
            pt.z = std::stod(sz);
            frame.polygonMm.push_back(pt);
        } catch (const std::exception&) {
            return DeviceResult<ContourFrame>::failure(
                DeviceError::InvalidArgument,
                "Non-numeric CSV row in " + pathUtf8 + ": " + line);
        }
    }

    if (frame.polygonMm.size() < 3) {
        return DeviceResult<ContourFrame>::failure(
            DeviceError::InvalidArgument,
            "Contour CSV needs at least 3 points: " + pathUtf8);
    }
    return DeviceResult<ContourFrame>::success(std::move(frame));
}

#if defined(OLP_HAS_NLOHMANN_JSON)
DeviceResult<ContourFrame> loadJsonFrame(
    const std::string& pathUtf8,
    const std::string& placementIdFallback) {
    std::ifstream in(olp::io::utf8ToPath(pathUtf8));
    if (!in) {
        return DeviceResult<ContourFrame>::failure(
            DeviceError::IoFailure,
            "Failed to open contour JSON: " + pathUtf8);
    }

    nlohmann::json root;
    try {
        in >> root;
    } catch (const std::exception& ex) {
        return DeviceResult<ContourFrame>::failure(
            DeviceError::InvalidArgument,
            std::string("JSON parse error in ") + pathUtf8 + ": " + ex.what());
    }

    // Field extraction (including polygonMm number conversion) must honor the
    // DeviceResult contract: never let nlohmann::json::type_error escape.
    try {
        ContourFrame frame;
        frame.placementId = root.value("placementId", placementIdFallback);
        frame.frameId = root.value("frameId", std::string("camera"));
        frame.boardThicknessMm = root.value("boardThicknessMm", 18.0);
        frame.fakePointCount = root.value("fakePointCount", 0);
        frame.timestampSec = root.value("timestampSec", 0.0);

        if (!root.contains("polygonMm") || !root["polygonMm"].is_array()) {
            return DeviceResult<ContourFrame>::failure(
                DeviceError::InvalidArgument,
                "JSON missing polygonMm array: " + pathUtf8);
        }

        for (const auto& item : root["polygonMm"]) {
            if (!item.is_array() || item.size() < 2) {
                return DeviceResult<ContourFrame>::failure(
                    DeviceError::InvalidArgument,
                    "polygonMm entries must be [x,y] or [x,y,z]: " + pathUtf8);
            }
            ContourPointMm pt;
            pt.x = item.at(0).get<double>();
            pt.y = item.at(1).get<double>();
            pt.z = (item.size() >= 3) ? item.at(2).get<double>() : 0.0;
            frame.polygonMm.push_back(pt);
        }

        if (frame.polygonMm.size() < 3) {
            return DeviceResult<ContourFrame>::failure(
                DeviceError::InvalidArgument,
                "polygonMm needs at least 3 points: " + pathUtf8);
        }
        return DeviceResult<ContourFrame>::success(std::move(frame));
    } catch (const nlohmann::json::exception& ex) {
        return DeviceResult<ContourFrame>::failure(
            DeviceError::InvalidArgument,
            std::string("JSON field type error in ") + pathUtf8 + ": " + ex.what());
    } catch (const std::exception& ex) {
        return DeviceResult<ContourFrame>::failure(
            DeviceError::InvalidArgument,
            std::string("JSON field error in ") + pathUtf8 + ": " + ex.what());
    }
}
#endif

} // namespace

DeviceResult<ContourDataset> ContourDataset::loadFromDirectory(
    const std::string& datasetDirUtf8) {
    namespace fs = std::filesystem;
    const fs::path dir = olp::io::utf8ToPath(datasetDirUtf8);
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
        return DeviceResult<ContourDataset>::failure(
            DeviceError::NotFound,
            "Vision Edge dataset directory not found: " + datasetDirUtf8);
    }

    ContourDataset dataset;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec || !entry.is_regular_file(ec)) {
            continue;
        }
        const std::string filename = entry.path().filename().string();
        if (filename.size() < 5) {
            continue;
        }
        // Contour placements only: placement_*.json / placement_*.csv.
        // Skip calib_*.json and other non-contour fixtures sharing this directory.
        const bool isJson = endsWithIgnoreCase(filename, ".json");
        const bool isCsv = endsWithIgnoreCase(filename, ".csv");
        if (!isJson && !isCsv) {
            continue;
        }
        std::string lowerName = filename;
        for (char& ch : lowerName) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        if (lowerName.rfind("placement_", 0) != 0) {
            continue;
        }

        const std::string placementId = stemFromFilename(filename);
        DeviceResult<ContourFrame> loaded;
        const std::string pathUtf8 = olp::io::pathToUtf8(entry.path());
#if defined(OLP_HAS_NLOHMANN_JSON)
        if (isJson) {
            loaded = loadJsonFrame(pathUtf8, placementId);
        } else {
            loaded = loadCsvFrame(pathUtf8, placementId);
        }
#else
        if (isJson) {
            loaded = DeviceResult<ContourFrame>::failure(
                DeviceError::NotSupported,
                "JSON contour requires nlohmann_json (OLP_HAS_NLOHMANN_JSON): " +
                    pathUtf8);
        } else {
            loaded = loadCsvFrame(pathUtf8, placementId);
        }
#endif
        if (!loaded.ok()) {
            return DeviceResult<ContourDataset>::failure(
                loaded.status.code, loaded.status.message);
        }

        // Prefer JSON over CSV when both exist for the same placement id.
        const auto existing = dataset.frames_.find(placementId);
        if (existing != dataset.frames_.end() && isCsv) {
            continue;
        }
        if (existing == dataset.frames_.end()) {
            dataset.placementIds_.push_back(placementId);
        }
        dataset.frames_[placementId] = std::move(loaded.value);
    }

    if (dataset.frames_.empty()) {
        return DeviceResult<ContourDataset>::failure(
            DeviceError::NotFound,
            "No placement contour files found under: " + datasetDirUtf8);
    }

    std::sort(dataset.placementIds_.begin(), dataset.placementIds_.end());
    return DeviceResult<ContourDataset>::success(std::move(dataset));
}

bool ContourDataset::hasPlacement(const std::string& placementId) const {
    return frames_.find(placementId) != frames_.end();
}

DeviceResult<ContourFrame> ContourDataset::frame(
    const std::string& placementId) const {
    const auto it = frames_.find(placementId);
    if (it == frames_.end()) {
        return DeviceResult<ContourFrame>::failure(
            DeviceError::NotFound,
            "Unknown placement id: " + placementId);
    }
    return DeviceResult<ContourFrame>::success(it->second);
}

std::string defaultVisionEdgeDatasetDir(const std::string& workspaceRootUtf8) {
    namespace fs = std::filesystem;
    fs::path root = olp::io::utf8ToPath(workspaceRootUtf8);
    return olp::io::pathToUtf8(root / "tests" / "data" / "vision_edge");
}

} // namespace olp::runtime
