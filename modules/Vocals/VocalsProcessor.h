#pragma once
#include "ChannelPluginProcessor.h"
#include "ChannelPluginEditor.h"

namespace livemix
{

// Dine Vocals: all behaviour lives in the shared ChannelPluginProcessor / ChannelPluginEditor
// (modules/Common); this class only names the product.
class VocalsProcessor : public ChannelPluginProcessor
{
public:
    VocalsProcessor() : ChannelPluginProcessor (Product::Vocals) {}
};

using VocalsEditor = ChannelPluginEditor;

} // namespace livemix
