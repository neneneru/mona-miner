#include "mona/core.hpp"
#include "mona_build_identity.hpp"

namespace mona {
BuildIdentity build_identity() {
    return {mona_build_identity::kBuildId, mona_build_identity::kBuildProfile,
            mona_build_identity::kSchema};
}
}
