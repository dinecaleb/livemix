#pragma once
#include "ChannelPluginEditor.h"
#include "DrumsProcessor.h"

namespace livemix
{

// Dine Drums editor: the shared Dine shell with the KIT view enabled by the product definition.
class DrumsEditor : public ChannelPluginEditor
{
public:
    explicit DrumsEditor (DrumsProcessor& p) : ChannelPluginEditor (p) {}
};

} // namespace livemix
