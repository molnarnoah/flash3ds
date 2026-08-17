#include "runtime/Movie.h"

namespace flash3ds::runtime {

size_t Movie::countTagsWithCode(uint16_t code) const {
    size_t n = 0;
    for (const auto& t : tags) {
        if (t.code == code) {
            ++n;
        }
    }
    return n;
}

}  // namespace flash3ds::runtime
