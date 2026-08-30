#pragma once

#include "market/Models.h"
#include "types/TypesXmlIO.h"

#include <vector>

namespace edity {

struct WorkspaceSnapshot {
    std::vector<MarketCategory> markets;
    std::vector<TraderFile> traders;
    std::vector<TraderZone> zones;
    std::vector<TypesDocument> typesFiles;
    std::vector<QuarantineFile> quarantine;
};

std::vector<ValidationIssue> ValidateWorkspace(const WorkspaceSnapshot& snap);

}  // namespace edity
