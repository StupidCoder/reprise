// The fourth adventure's definitions. See a4_definitions.hpp.
#include "system/a4_definitions.hpp"
#include <memory>

namespace picarat::adventure4 {

Definitions::Definitions(Assets& assets) {
    const auto list = assets.data("res/@/lt4_def.cb");
    for (const auto& c : list.commands) {
        Row row;
        for (unsigned i = 0; i < c.count; ++i) {
            const auto& o = list.operands[c.first + i];
            const auto kind = gss::Value::Kind(o.type & 15);
            if (kind == gss::Value::Kind::Text || kind == gss::Value::Kind::Name) {
                row.push_back(gss::Value::ofText(list.text(o)));
            } else if (kind == gss::Value::Kind::Float) {
                gss::Value v = gss::Value::ofInt(0);
                v.kind = gss::Value::Kind::Float;
                v.bits = o.value;
                row.push_back(v);
            } else {
                row.push_back(gss::Value::ofInt(std::int32_t(o.value)));
            }
        }
        rows_[c.opcode].push_back(std::move(row));
    }
}

const std::vector<Definitions::Row>& Definitions::rows(int opcode) const {
    static const std::vector<Row> none;
    const auto it = rows_.find(opcode);
    return it == rows_.end() ? none : it->second;
}

const Definitions::Row* Definitions::find(int opcode, std::size_t field, const gss::Value& key) const {
    for (const Row& r : rows(opcode)) {
        if (field >= r.size()) {
            continue;
        }
        const gss::Value& v = r[field];
        if (key.isText() ? v.isText() && v.text == key.text : !v.isText() && v.toInt() == key.toInt()) {
            return &r;
        }
    }
    return nullptr;
}

int Definitions::number(const Row& row, std::size_t field) {
    return field < row.size() && !row[field].isText() ? row[field].toInt() : 0;
}

std::string Definitions::text(const Row& row, std::size_t field) {
    return field < row.size() && row[field].isText() ? row[field].text : std::string();
}

const Definitions& definitions(Assets& assets) {
    // One set per asset folder.
    static std::map<std::string, std::unique_ptr<Definitions>> cache;
    auto& d = cache[assets.root.string()];
    if (!d) {
        d = std::make_unique<Definitions>(assets);
    }
    return *d;
}

} // namespace picarat::adventure4
