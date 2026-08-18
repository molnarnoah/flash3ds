#include "runtime/MovieClipInstance.h"

#include <algorithm>
#include <cmath>
#include <variant>

#include "avm1/GlobalObject.h"
#include "avm1/HostBindings.h"
#include "avm1/Interpreter.h"
#include "avm1/Scope.h"
#include "platform/Log.h"
#include "swf/TagCode.h"

namespace flash3ds::runtime {

namespace {

constexpr double kPi = 3.14159265358979323846;

}  // namespace

// ===========================================================================
// ScriptEnvironment
// ===========================================================================

ScriptEnvironment::ScriptEnvironment() : global_(avm1::GlobalObject::create()) {}

void ScriptEnvironment::scanInitActions(const Movie& movie) {
    for (const auto& tag : movie.tags) {
        if (static_cast<swf::TagCode>(tag.code) != swf::TagCode::DoInitAction) continue;
        swf::SwfReader reader = movie.tagBodyReader(tag);
        uint16_t spriteId = reader.readU16();
        if (reader.failed()) continue;
        initActionsByCharacter_[spriteId] = reader.readBytes(reader.bytesRemaining());
    }
}

const std::vector<uint8_t>* ScriptEnvironment::takeInitActionsOnce(uint16_t characterId) {
    if (initializedCharacters_.count(characterId)) return nullptr;
    auto it = initActionsByCharacter_.find(characterId);
    if (it == initActionsByCharacter_.end()) return nullptr;
    initializedCharacters_.insert(characterId);
    return &it->second;
}

avm1::Value ScriptEnvironment::run(MovieClipInstance& target, const uint8_t* code, size_t length) {
    // MovieClipHostBindings is defined below (this translation unit only —
    // AVM1 code only ever sees it through the abstract HostBindings seam).
    class MovieClipHostBindings : public avm1::HostBindings {
    public:
        explicit MovieClipHostBindings(MovieClipInstance& natural)
            : natural_(&natural), current_(&natural) {}

        MovieClipInstance* resolve(const std::string& target) {
            if (target.empty()) return current_;
            MovieClipInstance* r = natural_->resolvePath(target);
            return r ? r : nullptr;
        }

        void gotoFrame(uint32_t frameIndex) override {
            // AVM1's GotoFrame/GotoFrame2 frame numbers are 0-based;
            // Timeline's are 1-based (see Timeline.h).
            if (current_) current_->timeline().gotoAndStop(frameIndex + 1);
        }
        void gotoLabel(const std::string& label) override {
            if (current_) current_->timeline().gotoAndStop(label);
        }
        void play() override {
            if (current_) current_->timeline().play();
        }
        void stop() override {
            if (current_) current_->timeline().stop();
        }
        void nextFrame() override {
            if (current_) current_->timeline().nextFrame();
        }
        void previousFrame() override {
            if (current_) current_->timeline().prevFrame();
        }

        avm1::Value getProperty(const std::string& targetPath, int propertyIndex) override {
            MovieClipInstance* mc = resolve(targetPath);
            if (!mc) return avm1::Value::undefined();
            switch (propertyIndex) {
                case 0: return avm1::Value::number(mc->x());
                case 1: return avm1::Value::number(mc->y());
                case 2: return avm1::Value::number(mc->xScale());
                case 3: return avm1::Value::number(mc->yScale());
                case 4: return avm1::Value::number(mc->timeline().currentFrame());
                case 5: return avm1::Value::number(mc->timeline().frameCount());
                case 6: return avm1::Value::number(mc->alpha());
                case 7: return avm1::Value::boolean(mc->visible());
                case 8: return avm1::Value::number(0);  // _width — not computed, see header
                case 9: return avm1::Value::number(0);  // _height — not computed, see header
                case 10: return avm1::Value::number(mc->rotation());
                case 11: return avm1::Value::string(mc->targetPath());
                case 12: return avm1::Value::number(mc->timeline().frameCount());  // _framesloaded
                case 13: return avm1::Value::string(mc->name());
                case 14: return avm1::Value::string("");  // _droptarget — no drag model yet
                case 15: return avm1::Value::string("");  // _url
                default:
                    LOG_DEBUG("MOVIECLIP", "GetProperty: index %d not modeled — undefined",
                              propertyIndex);
                    return avm1::Value::undefined();
            }
        }
        void setProperty(const std::string& targetPath, int propertyIndex,
                          const avm1::Value& value) override {
            MovieClipInstance* mc = resolve(targetPath);
            if (!mc) return;
            switch (propertyIndex) {
                case 0: mc->setX(value.toNumber()); break;
                case 1: mc->setY(value.toNumber()); break;
                case 2: mc->setXScale(value.toNumber()); break;
                case 3: mc->setYScale(value.toNumber()); break;
                case 6: mc->setAlpha(value.toNumber()); break;
                case 7: mc->setVisible(value.toBoolean()); break;
                case 10: mc->setRotation(value.toNumber()); break;
                default:
                    LOG_DEBUG("MOVIECLIP", "SetProperty: index %d not modeled/read-only — ignored",
                              propertyIndex);
                    break;
            }
        }

        void cloneSprite(const std::string& targetPath, const std::string& newName,
                          int depth) override {
            MovieClipInstance* mc = resolve(targetPath);
            if (!mc) {
                LOG_WARN("MOVIECLIP", "CloneSprite: target '%s' not found", targetPath.c_str());
                return;
            }
            mc->cloneSprite(newName, depth);
        }
        void removeSprite(const std::string& targetPath) override {
            MovieClipInstance* mc = resolve(targetPath);
            if (!mc) return;
            mc->removeFromParent();
        }
        void startDrag(const std::string& targetPath) override {
            (void)targetPath;
            LOG_DEBUG("MOVIECLIP", "StartDrag — no input model yet (Phase 6)");
        }
        void endDrag() override {
            LOG_DEBUG("MOVIECLIP", "EndDrag — no input model yet (Phase 6)");
        }

        void setTarget(const std::string& targetPath) override {
            if (targetPath.empty()) {
                current_ = natural_;
                return;
            }
            MovieClipInstance* resolved = natural_->resolvePath(targetPath);
            if (resolved) {
                current_ = resolved;
            } else {
                LOG_WARN("MOVIECLIP", "SetTarget: '%s' did not resolve — target unchanged",
                          targetPath.c_str());
            }
        }

    private:
        MovieClipInstance* natural_;
        MovieClipInstance* current_;
    };

    MovieClipHostBindings host(target);

    avm1::Scope scope = avm1::Scope::topLevel(global_).pushed(target.scriptObject());
    avm1::ExecutionContext ctx(scope, global_);
    ctx.thisValue = avm1::Value::object(target.scriptObject());
    ctx.host = &host;

    return avm1::Interpreter::execute(ctx, code, length);
}

// ===========================================================================
// MovieClipInstance
// ===========================================================================

MovieClipInstance::MovieClipInstance(const Movie& movie, const CharacterDictionary& characters,
                                      ScriptEnvironment& env, MovieClipInstance* parent,
                                      std::string name, int32_t depthInParent,
                                      std::unique_ptr<Timeline> timeline)
    : movie_(&movie),
      characters_(&characters),
      env_(&env),
      parent_(parent),
      name_(std::move(name)),
      depthInParent_(depthInParent),
      timeline_(std::move(timeline)) {}

std::shared_ptr<MovieClipInstance> MovieClipInstance::createRoot(
    const Movie& movie, const CharacterDictionary& characters, ScriptEnvironment& env) {
    if (!movie.valid) return nullptr;
    auto timeline = Timeline::build(movie);
    if (!timeline || timeline->frameCount() == 0) return nullptr;

    env.scanInitActions(movie);

    std::shared_ptr<MovieClipInstance> root(new MovieClipInstance(
        movie, characters, env, nullptr, "", 0, std::move(timeline)));
    root->wireScriptObject();
    // Sync (place) children BEFORE running frame 1's script — matches a
    // real player, which builds the frame's display list from its
    // PlaceObject tags before running that frame's DoAction, so a script
    // referencing a clip placed on the SAME frame (`mc._x = 10;`) sees it.
    root->syncChildren();
    root->runCurrentFrameScripts();
    return root;
}

void MovieClipInstance::wireScriptObject() {
    scriptObject_ = std::make_shared<avm1::Object>();
    std::weak_ptr<MovieClipInstance> weak = shared_from_this();
    scriptObject_->nativeGet = [weak](const std::string& name, avm1::Value& out) -> bool {
        auto self = weak.lock();
        return self && self->handleNativeGet(name, out);
    };
    scriptObject_->nativeSet = [weak](const std::string& name, const avm1::Value& value) -> bool {
        auto self = weak.lock();
        return self && self->handleNativeSet(name, value);
    };
    scriptObject_->nativeEnumerate = [weak]() -> std::vector<std::string> {
        std::vector<std::string> names;
        if (auto self = weak.lock()) {
            names.reserve(self->childNameToDepth_.size());
            for (const auto& [n, depth] : self->childNameToDepth_) names.push_back(n);
        }
        return names;
    };
}

bool MovieClipInstance::handleNativeGet(const std::string& name, avm1::Value& out) const {
    if (name == "_x") { out = avm1::Value::number(x()); return true; }
    if (name == "_y") { out = avm1::Value::number(y()); return true; }
    if (name == "_xscale") { out = avm1::Value::number(xScale()); return true; }
    if (name == "_yscale") { out = avm1::Value::number(yScale()); return true; }
    if (name == "_rotation") { out = avm1::Value::number(rotation()); return true; }
    if (name == "_alpha") { out = avm1::Value::number(alpha()); return true; }
    if (name == "_visible") { out = avm1::Value::boolean(visible_); return true; }
    if (name == "_currentframe") { out = avm1::Value::number(timeline_->currentFrame()); return true; }
    if (name == "_totalframes" || name == "_framesloaded") {
        out = avm1::Value::number(timeline_->frameCount());
        return true;
    }
    if (name == "_name") { out = avm1::Value::string(name_); return true; }
    if (name == "_target") { out = avm1::Value::string(targetPath()); return true; }
    if (name == "_droptarget" || name == "_url") { out = avm1::Value::string(""); return true; }
    if (name == "_width" || name == "_height") { out = avm1::Value::number(0); return true; }
    if (name == "_parent") {
        out = parent_ ? avm1::Value::object(parent_->scriptObject_) : avm1::Value::undefined();
        return true;
    }
    if (name == "_root") {
        auto* self = const_cast<MovieClipInstance*>(this);
        out = avm1::Value::object(self->rootInstance().scriptObject_);
        return true;
    }
    if (name == "_global") { out = avm1::Value::object(env_->globalObject()); return true; }

    auto it = childNameToDepth_.find(name);
    if (it != childNameToDepth_.end()) {
        auto childIt = children_.find(it->second);
        if (childIt != children_.end() && childIt->second) {
            out = avm1::Value::object(childIt->second->scriptObject_);
            return true;
        }
    }
    return false;
}

bool MovieClipInstance::handleNativeSet(const std::string& name, const avm1::Value& value) {
    if (name == "_x") { setX(value.toNumber()); return true; }
    if (name == "_y") { setY(value.toNumber()); return true; }
    if (name == "_xscale") { setXScale(value.toNumber()); return true; }
    if (name == "_yscale") { setYScale(value.toNumber()); return true; }
    if (name == "_rotation") { setRotation(value.toNumber()); return true; }
    if (name == "_alpha") { setAlpha(value.toNumber()); return true; }
    if (name == "_visible") { visible_ = value.toBoolean(); return true; }
    static const char* kReadOnly[] = {
        "_currentframe", "_totalframes", "_framesloaded", "_name",  "_target",
        "_droptarget",   "_url",         "_width",        "_height", "_parent",
        "_root",         "_global",
    };
    for (const char* ro : kReadOnly) {
        if (name == ro) {
            LOG_DEBUG("MOVIECLIP", "ignoring write to read-only property '%s'", name.c_str());
            return true;
        }
    }
    return false;
}

void MovieClipInstance::initializeNewlyCreated() {
    if (const std::vector<uint8_t>* init = env_->takeInitActionsOnce(characterId_)) {
        env_->run(*this, init->data(), init->size());
    }
    // Same place-before-script ordering as createRoot() — see its comment.
    syncChildren();
    runCurrentFrameScripts();
}

void MovieClipInstance::runCurrentFrameScripts() {
    for (const auto& bytes : timeline_->currentFrameDoActionBodies()) {
        env_->run(*this, bytes.data(), bytes.size());
    }
}

void MovieClipInstance::syncChildren() {
    const DisplayList& dl = timeline_->displayList();

    // 1) Remove children whose depth no longer has an entry, or whose
    // entry's characterId changed (a genuine replacement, not the same
    // instance continuing).
    for (auto it = children_.begin(); it != children_.end();) {
        const DisplayListEntry* entry = dl.find(it->first);
        bool stillValid = entry && entry->characterId == it->second->characterId_;
        if (!stillValid) {
            for (auto nameIt = childNameToDepth_.begin(); nameIt != childNameToDepth_.end();) {
                if (nameIt->second == it->first) nameIt = childNameToDepth_.erase(nameIt);
                else ++nameIt;
            }
            it = children_.erase(it);
        } else {
            ++it;
        }
    }

    // 2) For every depth in the current display list that resolves to a
    // Sprite character and doesn't already have a live child, create one
    // and run its initial (frame 1 / DoInitAction) scripts. See the class
    // header for why a SURVIVING child's transform is deliberately NOT
    // re-copied from the display-list entry here.
    if (childrenSyncDepth_ >= kMaxDepth) {
        LOG_WARN("MOVIECLIP",
                  "recursion depth limit (%d) exceeded while syncing children — possible cyclic "
                  "sprite reference; not creating new children this pass",
                  kMaxDepth);
        return;
    }

    for (const auto& [depthValue, entry] : dl.entries()) {
        if (children_.count(depthValue)) continue;

        const CharacterDef* def = characters_->find(entry.characterId);
        if (!def || !std::holds_alternative<SpriteDef>(*def)) continue;

        const SpriteDef& spriteDef = std::get<SpriteDef>(*def);
        auto childTimeline = Timeline::build(*movie_, spriteDef.tags);
        if (!childTimeline) continue;

        std::string childName = entry.name.value_or("");
        std::shared_ptr<MovieClipInstance> child(new MovieClipInstance(
            *movie_, *characters_, *env_, this, childName, depthValue, std::move(childTimeline)));
        child->characterId_ = entry.characterId;
        child->matrix_ = entry.matrix;
        child->colorTransform_ = entry.colorTransform;
        child->childrenSyncDepth_ = childrenSyncDepth_ + 1;
        child->wireScriptObject();

        children_[depthValue] = child;
        if (!childName.empty()) childNameToDepth_[childName] = depthValue;

        child->initializeNewlyCreated();
    }
}

void MovieClipInstance::advanceFrame() {
    if (timeline_->isPlaying()) {
        timeline_->advanceOneFrame();
    }
    // Place-before-script ordering — see createRoot()'s comment.
    syncChildren();
    runCurrentFrameScripts();
    // Copy the child list before recursing: a script that ran just above
    // (via runCurrentFrameScripts/syncChildren) could have removed a child
    // via RemoveSprite, and a child's own advanceFrame() could in turn
    // trigger further CloneSprite/RemoveSprite calls on ITS children —
    // iterating a live map while it mutates is undefined behavior, so walk
    // a snapshot instead.
    std::vector<std::shared_ptr<MovieClipInstance>> snapshot;
    snapshot.reserve(children_.size());
    for (auto& [depthValue, child] : children_) {
        (void)depthValue;
        snapshot.push_back(child);
    }
    for (auto& child : snapshot) {
        child->advanceFrame();
    }
}

std::string MovieClipInstance::targetPath() const {
    if (!parent_) return "/";
    std::string path;
    const MovieClipInstance* cursor = this;
    std::vector<std::string> segments;
    while (cursor && cursor->parent_) {
        segments.push_back(cursor->name_);
        cursor = cursor->parent_;
    }
    for (auto it = segments.rbegin(); it != segments.rend(); ++it) {
        path += "/" + *it;
    }
    return path.empty() ? "/" : path;
}

MovieClipInstance& MovieClipInstance::rootInstance() {
    MovieClipInstance* cursor = this;
    while (cursor->parent_) cursor = cursor->parent_;
    return *cursor;
}

MovieClipInstance* MovieClipInstance::resolvePath(const std::string& pathIn) {
    if (pathIn.empty()) return this;

    std::string path = pathIn;
    bool absolute = false;
    if (path[0] == '/') {
        absolute = true;
        path = path.substr(1);
    } else if (path.rfind("_root", 0) == 0 &&
               (path.size() == 5 || path[5] == '.' || path[5] == '/')) {
        absolute = true;
        path = path.size() == 5 ? "" : path.substr(6);
    }
    // Dot-syntax ("a.b.c") and slash-syntax ("a/b/c", "../c", "..") are
    // mutually exclusive in real AS2 content — legitimate dot-syntax member
    // chains never contain two consecutive dots, so treat any "/" OR ".."
    // as a sign this is already slash-syntax and leave it alone (blindly
    // replacing every '.' would mangle ".." into "//").
    if (path.find('/') == std::string::npos && path.find("..") == std::string::npos) {
        std::replace(path.begin(), path.end(), '.', '/');
    }

    MovieClipInstance* cursor = absolute ? &rootInstance() : this;
    if (path.empty()) return cursor;

    size_t start = 0;
    while (cursor && start <= path.size()) {
        size_t slash = path.find('/', start);
        std::string segment =
            path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (segment == "..") {
            cursor = cursor->parent_ ? cursor->parent_ : cursor;
        } else if (!segment.empty()) {
            auto it = cursor->childNameToDepth_.find(segment);
            if (it == cursor->childNameToDepth_.end()) return nullptr;
            auto childIt = cursor->children_.find(it->second);
            if (childIt == cursor->children_.end() || !childIt->second) return nullptr;
            cursor = childIt->second.get();
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return cursor;
}

void MovieClipInstance::setX(double px) {
    matrix_.translateXTwips = static_cast<int32_t>(std::lround(px * 20.0));
}
void MovieClipInstance::setY(double px) {
    matrix_.translateYTwips = static_cast<int32_t>(std::lround(px * 20.0));
}

double MovieClipInstance::xScale() const {
    return std::sqrt(matrix_.scaleX * matrix_.scaleX + matrix_.rotateSkew0 * matrix_.rotateSkew0) *
           100.0;
}
double MovieClipInstance::yScale() const {
    return std::sqrt(matrix_.scaleY * matrix_.scaleY + matrix_.rotateSkew1 * matrix_.rotateSkew1) *
           100.0;
}
double MovieClipInstance::rotation() const {
    return std::atan2(matrix_.rotateSkew0, matrix_.scaleX) * 180.0 / kPi;
}

void MovieClipInstance::setXScale(double percent) {
    double theta = std::atan2(matrix_.rotateSkew0, matrix_.scaleX);
    double s = percent / 100.0;
    matrix_.scaleX = s * std::cos(theta);
    matrix_.rotateSkew0 = s * std::sin(theta);
}
void MovieClipInstance::setYScale(double percent) {
    double theta = std::atan2(-matrix_.rotateSkew1, matrix_.scaleY);
    double s = percent / 100.0;
    matrix_.scaleY = s * std::cos(theta);
    matrix_.rotateSkew1 = -s * std::sin(theta);
}
void MovieClipInstance::setRotation(double degrees) {
    double theta = degrees * kPi / 180.0;
    double sx = xScale() / 100.0;
    double sy = yScale() / 100.0;
    matrix_.scaleX = sx * std::cos(theta);
    matrix_.rotateSkew0 = sx * std::sin(theta);
    matrix_.rotateSkew1 = -sy * std::sin(theta);
    matrix_.scaleY = sy * std::cos(theta);
}

std::shared_ptr<MovieClipInstance> MovieClipInstance::cloneSprite(const std::string& newName,
                                                                    int32_t depth) {
    if (!parent_) {
        LOG_WARN("MOVIECLIP", "CloneSprite: the root clip cannot be cloned");
        return nullptr;
    }
    const CharacterDef* def = characters_->find(characterId_);
    if (!def || !std::holds_alternative<SpriteDef>(*def)) {
        LOG_WARN("MOVIECLIP", "CloneSprite: source character %u is not a sprite", characterId_);
        return nullptr;
    }

    swf::PlaceObjectRecord record;
    record.version = 2;
    record.depth = depth;
    record.move = false;
    record.characterId = characterId_;
    record.matrix = matrix_;
    record.colorTransform = colorTransform_;
    record.name = newName;
    parent_->timeline_->mutableDisplayListForScripting().applyPlaceObject(record);

    // Remove any existing occupant at this depth first (a clone can
    // legitimately overwrite one, per spec).
    auto existing = parent_->children_.find(depth);
    if (existing != parent_->children_.end()) {
        for (auto nameIt = parent_->childNameToDepth_.begin();
             nameIt != parent_->childNameToDepth_.end();) {
            if (nameIt->second == depth) nameIt = parent_->childNameToDepth_.erase(nameIt);
            else ++nameIt;
        }
        parent_->children_.erase(existing);
    }

    const SpriteDef& spriteDef = std::get<SpriteDef>(*def);
    auto childTimeline = Timeline::build(*movie_, spriteDef.tags);
    if (!childTimeline) return nullptr;

    std::shared_ptr<MovieClipInstance> clone(new MovieClipInstance(
        *movie_, *characters_, *env_, parent_, newName, depth, std::move(childTimeline)));
    clone->characterId_ = characterId_;
    clone->matrix_ = matrix_;
    clone->colorTransform_ = colorTransform_;
    clone->childrenSyncDepth_ = childrenSyncDepth_;
    clone->wireScriptObject();

    parent_->children_[depth] = clone;
    if (!newName.empty()) parent_->childNameToDepth_[newName] = depth;

    clone->initializeNewlyCreated();
    return clone;
}

void MovieClipInstance::removeFromParent() {
    if (!parent_) {
        LOG_WARN("MOVIECLIP", "RemoveSprite: the root clip cannot be removed");
        return;
    }
    parent_->timeline_->mutableDisplayListForScripting().remove(depthInParent_);
    for (auto nameIt = parent_->childNameToDepth_.begin();
         nameIt != parent_->childNameToDepth_.end();) {
        if (nameIt->second == depthInParent_) nameIt = parent_->childNameToDepth_.erase(nameIt);
        else ++nameIt;
    }
    // Erasing from children_ may destroy `this` (it's the last shared_ptr
    // reference in the common case) — do it last, and don't touch any
    // member after this line.
    parent_->children_.erase(depthInParent_);
}

}  // namespace flash3ds::runtime
