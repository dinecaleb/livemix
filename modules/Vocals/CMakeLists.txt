# Dine Vocals: AU + Standalone built from the shared channel-plugin base (modules/Common).
dine_add_channel_product(LiveMixVocals
    PRODUCT Vocals
    NAME "Dine Vocals"
    BUNDLE_ID "com.dine.vocals"
    PLUGIN_CODE Lmvo
    SOURCES VocalsProcessor.cpp)
