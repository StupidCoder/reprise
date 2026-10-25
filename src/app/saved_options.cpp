#include "app/saved_options.hpp"
#include <algorithm>
#include <fstream>
#include <map>

namespace app {

namespace {

std::string trimmed(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

} // namespace

std::string options_text(const GameModule& module) {
    std::string text;
    for (const auto& o : module.options()) {
        if (o.value >= 0 && std::size_t(o.value) < o.choices.size()) {
            text += o.name + " = " + o.choices[std::size_t(o.value)] + "\n";
        }
    }
    return text;
}

void restore_options(GameModule& module, const std::filesystem::path& file) {
    std::ifstream in(file);
    std::map<std::string, std::string> saved;
    for (std::string line; std::getline(in, line);) {
        const auto equals = line.find('=');
        if (equals != std::string::npos) {
            saved[trimmed(line.substr(0, equals))] = trimmed(line.substr(equals + 1));
        }
    }
    const auto options = module.options();
    for (std::size_t i = 0; i < options.size(); ++i) {
        const auto it = saved.find(options[i].name);
        if (it == saved.end()) {
            continue;
        }
        const auto& choices = options[i].choices;
        const auto choice = std::find(choices.begin(), choices.end(), it->second);
        if (choice != choices.end()) {
            module.set_option(i, int(choice - choices.begin()));
        }
    }
}

} // namespace app
