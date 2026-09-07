#pragma once
#include "ChannelPluginProcessor.h"
#include "ChannelPluginEditor.h"

namespace livemix
{

// Dine Keys: all behaviour lives in the shared ChannelPluginProcessor / ChannelPluginEditor
// (modules/Common); this class only names the product.
class KeysProcessor : public ChannelPluginProcessor
{
public:
    KeysProcessor() : ChannelPluginProcessor (Product::Keys) {}
};

using KeysEditor = ChannelPluginEditor;

} // namespace livemix
