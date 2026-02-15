#pragma once

#include "query.h"
#include "queryset.h"

namespace saengra {

struct StartPositions {
    const std::vector<Position> positions;
    const std::vector<QuerySetObservable> deps;
};

struct StartPositionsDraft {
    std::vector<Position> positions;
    std::vector<QuerySetObservable> deps;

    StartPositions finalize() const;
};

class StartPositionFinder {
public:
    StartPositions find_start_positions(const Query& query) const;
};

}
