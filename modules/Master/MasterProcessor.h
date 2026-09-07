#pragma once
#include "ChannelPluginProcessor.h"
#include "ChannelPluginEditor.h"

namespace livemix
{

// Dine Master: all behaviour lives in the shared ChannelPluginProcessor / ChannelPluginEditor
// (modules/Common); this class only names the product.
class MasterProcessor : public ChannelPluginProcessor
{
public:
    MasterProcessor() : ChannelPluginProcessor (Product::Master) {}
};

using MasterEditor = ChannelPluginEditor;

} // namespace livemix
