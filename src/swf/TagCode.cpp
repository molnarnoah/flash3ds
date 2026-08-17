#include "swf/TagCode.h"

namespace flash3ds::swf {

const char* tagCodeName(uint16_t code) {
    switch (static_cast<TagCode>(code)) {
        case TagCode::End: return "End";
        case TagCode::ShowFrame: return "ShowFrame";
        case TagCode::DefineShape: return "DefineShape";
        case TagCode::PlaceObject: return "PlaceObject";
        case TagCode::RemoveObject: return "RemoveObject";
        case TagCode::DefineBits: return "DefineBits";
        case TagCode::DefineButton: return "DefineButton";
        case TagCode::JpegTables: return "JpegTables";
        case TagCode::SetBackgroundColor: return "SetBackgroundColor";
        case TagCode::DefineFont: return "DefineFont";
        case TagCode::DefineText: return "DefineText";
        case TagCode::DoAction: return "DoAction";
        case TagCode::DefineFontInfo: return "DefineFontInfo";
        case TagCode::DefineSound: return "DefineSound";
        case TagCode::StartSound: return "StartSound";
        case TagCode::DefineButtonSound: return "DefineButtonSound";
        case TagCode::SoundStreamHead: return "SoundStreamHead";
        case TagCode::SoundStreamBlock: return "SoundStreamBlock";
        case TagCode::DefineBitsLossless: return "DefineBitsLossless";
        case TagCode::DefineBitsJpeg2: return "DefineBitsJpeg2";
        case TagCode::DefineShape2: return "DefineShape2";
        case TagCode::DefineButtonCxform: return "DefineButtonCxform";
        case TagCode::Protect: return "Protect";
        case TagCode::PlaceObject2: return "PlaceObject2";
        case TagCode::RemoveObject2: return "RemoveObject2";
        case TagCode::DefineShape3: return "DefineShape3";
        case TagCode::DefineText2: return "DefineText2";
        case TagCode::DefineButton2: return "DefineButton2";
        case TagCode::DefineBitsJpeg3: return "DefineBitsJpeg3";
        case TagCode::DefineBitsLossless2: return "DefineBitsLossless2";
        case TagCode::DefineEditText: return "DefineEditText";
        case TagCode::DefineSprite: return "DefineSprite";
        case TagCode::FrameLabel: return "FrameLabel";
        case TagCode::SoundStreamHead2: return "SoundStreamHead2";
        case TagCode::DefineMorphShape: return "DefineMorphShape";
        case TagCode::DefineFont2: return "DefineFont2";
        case TagCode::ExportAssets: return "ExportAssets";
        case TagCode::ImportAssets: return "ImportAssets";
        case TagCode::EnableDebugger: return "EnableDebugger";
        case TagCode::DoInitAction: return "DoInitAction";
        case TagCode::DefineVideoStream: return "DefineVideoStream";
        case TagCode::VideoFrame: return "VideoFrame";
        case TagCode::DefineFontInfo2: return "DefineFontInfo2";
        case TagCode::EnableDebugger2: return "EnableDebugger2";
        case TagCode::ScriptLimits: return "ScriptLimits";
        case TagCode::SetTabIndex: return "SetTabIndex";
        case TagCode::FileAttributes: return "FileAttributes";
        case TagCode::PlaceObject3: return "PlaceObject3";
        case TagCode::ImportAssets2: return "ImportAssets2";
        case TagCode::DoABC: return "DoABC";
        case TagCode::DefineFontAlignZones: return "DefineFontAlignZones";
        case TagCode::CsmTextSettings: return "CsmTextSettings";
        case TagCode::DefineFont3: return "DefineFont3";
        case TagCode::SymbolClass: return "SymbolClass";
        case TagCode::Metadata: return "Metadata";
        case TagCode::DefineScalingGrid: return "DefineScalingGrid";
        case TagCode::DoABC2: return "DoABC2";
        case TagCode::DefineShape4: return "DefineShape4";
        case TagCode::DefineMorphShape2: return "DefineMorphShape2";
        case TagCode::DefineSceneAndFrameLabelData: return "DefineSceneAndFrameLabelData";
        case TagCode::DefineBinaryData: return "DefineBinaryData";
        case TagCode::DefineFontName: return "DefineFontName";
        case TagCode::DefineBitsJpeg4: return "DefineBitsJpeg4";
        case TagCode::DefineFont4: return "DefineFont4";
        default: return "Unknown";
    }
}

}  // namespace flash3ds::swf
