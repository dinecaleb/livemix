#pragma once
#include "ChannelPluginProcessor.h"
#include "ChannelPluginEditor.h"

namespace livemix
{

// Dine Guitar: all behaviour lives in the shared ChannelPluginProcessor / ChannelPluginEditor
// (modules/Common); this class only names the product.
class GuitarProcessor : public ChannelPluginProcessor
{
public:
    GuitarProcessor() : ChannelPluginProcessor (Product::Guitar) {}
};

using GuitarEditor = ChannelPluginEditor;

} // namespace livemix
