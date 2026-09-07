#pragma once
#include "ChannelPluginProcessor.h"
#include "ChannelPluginEditor.h"

namespace livemix
{

// Dine Bass: all behaviour lives in the shared ChannelPluginProcessor / ChannelPluginEditor
// (modules/Common); this class only names the product.
class BassProcessor : public ChannelPluginProcessor
{
public:
    BassProcessor() : ChannelPluginProcessor (Product::Bass) {}
};

using BassEditor = ChannelPluginEditor;

} // namespace livemix
