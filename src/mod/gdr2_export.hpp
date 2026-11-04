#pragma once

#include "mod/plan_edges.hpp"
#include "mod/gdr/gdr.hpp"
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <system_error>

namespace gdsolver::macro {

inline std::string sanitizeFilename(const std::string& name) {
    std::string safe;
    safe.reserve(name.size());
    for (char c : name) {
        if (static_cast<unsigned char>(c) < 32 ||
            c == '<' || c == '>' || c == ':' || c == '"' ||
            c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') {
            safe += '_';
        } else {
            safe += c;
        }
    }
    while (!safe.empty() && (safe.back() == ' ' || safe.back() == '.')) {
        safe.pop_back();
    }
    return safe;
}

inline bool exportGdr2(const std::filesystem::path& exportDir,
                       int levelId,
                       const std::string& levelName,
                       int coins,
                       const std::vector<p1::InputCmd>& plan,
                       std::string& outFilename,
                       std::string& outError,
                       int frameOffset = 0) {
    if (plan.empty()) {
        outError = "Plan has no inputs";
        return false;
    }

    std::string safeName = sanitizeFilename(levelName);
    if (safeName.empty()) {
        safeName = "level_" + std::to_string(levelId);
    }
    outFilename = safeName + "-macro.gdr2";

    std::error_code ec;
    std::filesystem::create_directories(exportDir, ec);
    if (ec) {
        outError = "Could not create directory: " + ec.message();
        return false;
    }

    std::filesystem::path fullPath = exportDir / outFilename;

    gdr::Replay<> replay("gdsolver", 700);
    replay.author = "gdsolver";
    replay.description = "Solved by gdsolver";
#ifdef GEODE_COMP_GD_VERSION
    replay.gameVersion = GEODE_COMP_GD_VERSION;
#else
    replay.gameVersion = 22081;
#endif
    replay.framerate = 240.0;
    replay.ldm = false;
    replay.platformer = false;
    replay.coins = coins;
    replay.levelInfo.id = static_cast<uint32_t>(levelId);
    replay.levelInfo.name = levelName;

    std::vector<p1::InputCmd> sortedPlan = plan;
    std::stable_sort(sortedPlan.begin(), sortedPlan.end(), [](const auto& a, const auto& b) {
        if (a.p2 != b.p2) return !a.p2 && b.p2;
        return a.step < b.step;
    });

    uint64_t maxFrame = 0;
    for (const auto& cmd : sortedPlan) {
        // frameOffset shifts every input by whole frames, for a player that counts frames differently
        // from the mod (the mod applies step N just before physics step N, counting from 0).
        uint64_t frame = static_cast<uint64_t>(std::max(0, cmd.step + frameOffset));
        if (frame > maxFrame) maxFrame = frame;
        replay.inputs.emplace_back(frame, static_cast<uint8_t>(1) /* Jump */, cmd.p2, cmd.down);
    }

    replay.duration = static_cast<float>(maxFrame) / 240.0f;

    auto res = replay.exportData(fullPath);
    if (res.isErr()) {
        outError = res.unwrapErr();
        return false;
    }

    return true;
}

} // namespace gdsolver::macro
