// Message files (MSBT, the LibMessageStudio binary text format; general structure from public
// format documentation): labelled UTF-16 strings with inline control tags. The game's texts live
// in LocalizedData/<region language>/MessageData/{LayoutMessage,SystemMessage}.szs, one MSBT per
// screen or topic (StageName.msbt holds the course names, keyed by stage name).
#pragma once
#include "Binary.hpp"
#include "Json.hpp"

namespace captain_toad {

// {"labels": {label: index}, "texts": [[run, ...], ...]} where a run is a UTF-8 string or a tag
// object {"tag": [group, type], "data": "hex"} (tag end markers: {"end": [group, type]}).
json::Value readMsbt(const Bytes& b);

}   // namespace captain_toad
