#include "TestFramework.h"
#include "runtime/DisplayList.h"

using flash3ds::runtime::DisplayList;
using flash3ds::swf::Matrix;
using flash3ds::swf::PlaceObjectRecord;

TEST_CASE(DisplayList_AddNew_ViaMoveFalse) {
    DisplayList dl;
    PlaceObjectRecord rec;
    rec.version = 2;
    rec.depth = 3;
    rec.move = false;
    rec.characterId = 10;
    Matrix m;
    m.translateXTwips = 100;
    rec.matrix = m;

    dl.applyPlaceObject(rec);

    CHECK_EQ(dl.size(), 1u);
    auto* entry = dl.find(3);
    CHECK(entry != nullptr);
    CHECK_EQ(entry->characterId, 10);
    CHECK_EQ(entry->matrix.translateXTwips, 100);
}

TEST_CASE(DisplayList_UpdateInPlace_KeepsCharacterId) {
    DisplayList dl;

    PlaceObjectRecord add;
    add.depth = 3;
    add.move = false;
    add.characterId = 10;
    dl.applyPlaceObject(add);

    PlaceObjectRecord update;
    update.depth = 3;
    update.move = true;  // Move=1, no characterId -> update transform only
    Matrix m;
    m.translateXTwips = 250;
    update.matrix = m;
    dl.applyPlaceObject(update);

    CHECK_EQ(dl.size(), 1u);
    auto* entry = dl.find(3);
    CHECK(entry != nullptr);
    CHECK_EQ(entry->characterId, 10);  // unchanged
    CHECK_EQ(entry->matrix.translateXTwips, 250);  // updated
}

TEST_CASE(DisplayList_Replace_ChangesCharacterId) {
    DisplayList dl;

    PlaceObjectRecord add;
    add.depth = 3;
    add.move = false;
    add.characterId = 10;
    dl.applyPlaceObject(add);

    PlaceObjectRecord replace;
    replace.depth = 3;
    replace.move = true;
    replace.characterId = 20;  // Move=1 + HasCharacter -> replace
    dl.applyPlaceObject(replace);

    CHECK_EQ(dl.size(), 1u);
    auto* entry = dl.find(3);
    CHECK(entry != nullptr);
    CHECK_EQ(entry->characterId, 20);
}

TEST_CASE(DisplayList_UpdateOnEmptyDepth_IsNoOp) {
    DisplayList dl;
    PlaceObjectRecord update;
    update.depth = 3;
    update.move = true;  // no characterId, nothing at depth 3
    dl.applyPlaceObject(update);
    CHECK_EQ(dl.size(), 0u);
}

TEST_CASE(DisplayList_Remove) {
    DisplayList dl;
    PlaceObjectRecord add;
    add.depth = 3;
    add.characterId = 10;
    dl.applyPlaceObject(add);
    CHECK_EQ(dl.size(), 1u);

    dl.remove(3);
    CHECK_EQ(dl.size(), 0u);
    CHECK(dl.find(3) == nullptr);

    dl.remove(999);  // no-op, doesn't crash
    CHECK_EQ(dl.size(), 0u);
}

TEST_CASE(DisplayList_MultipleDepths_Independent) {
    DisplayList dl;
    for (int32_t depth = 0; depth < 5; ++depth) {
        PlaceObjectRecord rec;
        rec.depth = depth;
        rec.characterId = static_cast<uint16_t>(100 + depth);
        dl.applyPlaceObject(rec);
    }
    CHECK_EQ(dl.size(), 5u);
    dl.remove(2);
    CHECK_EQ(dl.size(), 4u);
    CHECK(dl.find(2) == nullptr);
    CHECK(dl.find(1)->characterId == 101);
}
