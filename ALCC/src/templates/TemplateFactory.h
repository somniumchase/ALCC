#ifndef ALCC_TEMPLATE_FACTORY_H
#define ALCC_TEMPLATE_FACTORY_H

#include "AlccTemplate.h"
#include <map>
#include <string>
#include <memory>
#include <vector>

class TemplateFactory {
public:
    static TemplateFactory& instance();

    void register_template(AlccTemplate* t);
    AlccTemplate* get_template(const std::string& name);
    std::vector<std::string> get_available_templates() const;

private:
    std::map<std::string, AlccTemplate*> templates;
    TemplateFactory() {}
};

#endif
