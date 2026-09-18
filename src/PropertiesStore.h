#pragma once

#include <string>
#include <vector>

#include "Types.h"

// Holds the name/value properties most recently fetched for the selected
// object (a properties_response, answering a get_properties request) --
// same static/global state pattern as SourceStore/DiagnosisStore.
class PropertiesStore {
  public:
    // subject: human-readable label for what these properties belong to
    // (e.g. an instance or term path), shown as a heading by PropertiesView.
    static void setProperties(const std::string& subject, std::vector<PropertyItem> properties);
    static void clear();

    static bool hasProperties();
    static const std::string& subject();
    static const std::vector<PropertyItem>& properties();
};
