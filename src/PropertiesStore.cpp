#include "PropertiesStore.h"

namespace {
std::string                  g_subject;
std::vector<PropertyItem>    g_properties;
bool                          g_hasProperties = false;
} // namespace

void PropertiesStore::setProperties(const std::string& subject, std::vector<PropertyItem> properties) {
  g_subject       = subject;
  g_properties    = std::move(properties);
  g_hasProperties = true;
}

void PropertiesStore::clear() {
  g_subject.clear();
  g_properties.clear();
  g_hasProperties = false;
}

bool PropertiesStore::hasProperties() { return g_hasProperties; }
const std::string& PropertiesStore::subject() { return g_subject; }
const std::vector<PropertyItem>& PropertiesStore::properties() { return g_properties; }
