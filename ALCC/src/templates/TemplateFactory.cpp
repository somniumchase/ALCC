#include "TemplateFactory.h"
#include <iostream>

TemplateFactory& TemplateFactory::instance() {
    static TemplateFactory factory;
    return factory;
}

void TemplateFactory::register_template(AlccTemplate* t) {
    templates[t->get_name()] = t;
}

AlccTemplate* TemplateFactory::get_template(const std::string& name) {
    if (templates.find(name) != templates.end()) {
        return templates[name];
    }
    return nullptr;
}

std::vector<std::string> TemplateFactory::get_available_templates() const {
    std::vector<std::string> result;
    for (auto const& [key, val] : templates) {
        result.push_back(key);
    }
    return result;
}
