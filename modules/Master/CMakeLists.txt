# Dine Master: AU + Standalone built from the shared channel-plugin base (modules/Common).
dine_add_channel_product(LiveMixMaster
    PRODUCT Master
    NAME "Dine Master"
    BUNDLE_ID "com.dine.master"
    PLUGIN_CODE Lmma
    SOURCES MasterProcessor.cpp)
