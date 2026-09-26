#include "shared/ui/PanelRegistry.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace vc::ui {
namespace {

std::string readFile(std::filesystem::path const& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

void appendError(std::string* error, std::string const& message) {
    if (!error) return;
    if (!error->empty()) error->append("; ");
    error->append(message);
}

} // namespace

std::vector<PanelDefinition> PanelRegistry::loadFromDirectory(
    std::filesystem::path const& dir,
    std::string* error
) {
    std::vector<PanelDefinition> panels;

    std::error_code code;
    if (!std::filesystem::is_directory(dir, code)) {
        appendError(error, "panel directory not found: " + dir.string());
        return panels;
    }

    std::vector<std::filesystem::path> files;
    for (auto const& entry : std::filesystem::directory_iterator(dir, code)) {
        if (!entry.is_regular_file(code)) continue;
        if (entry.path().extension() != ".json") continue;
        files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    for (auto const& path : files) {
        auto const text = readFile(path);
        if (text.empty()) {
            appendError(error, "panel file is empty or unreadable: " + path.filename().string());
            continue;
        }
        std::string parseError;
        auto definition = PanelDefinition::parse(text, parseError);
        if (!definition) {
            appendError(error, path.filename().string() + ": " + parseError);
            continue;
        }
        panels.push_back(std::move(*definition));
    }

    return panels;
}

void PanelRegistry::load(std::filesystem::path const& dir, std::string* error) {
    panels_ = loadFromDirectory(dir, error);
}

PanelDefinition const* PanelRegistry::find(std::string const& id) const {
    for (auto const& panel : panels_) {
        if (panel.id == id) return &panel;
    }
    return nullptr;
}

} // namespace vc::ui
