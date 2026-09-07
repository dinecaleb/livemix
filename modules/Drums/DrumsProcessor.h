#pragma once
#include "ChannelPluginProcessor.h"

namespace livemix
{

// Dine Drums: one channel of the drum kit. All behaviour lives in the shared
// ChannelPluginProcessor; this class only names the product.
class DrumsProcessor : public ChannelPluginProcessor
{
public:
    DrumsProcessor() : ChannelPluginProcessor (Product::Drums) {}
};

} // namespace livemix
