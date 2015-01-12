// Double quotes, spaces OK.
#define PLUG_MFR            "Tale"
#define PLUG_NAME           "IPlug Verb"

// No quotes or spaces.
#define PLUG_CLASS_NAME      IPlugVerb

// OSX crap.
// - Manually edit the info.plist file to set the CFBundleIdentifier to the either the string 
// "com.BUNDLE_MFR.audiounit.BUNDLE_NAME" or "com.BUNDLE_MFR.vst.BUNDLE_NAME".
// Double quotes, no spaces.
#define BUNDLE_MFR          "Tale"
#define BUNDLE_NAME         "IPlugVerb"
// - Manually create a PLUG_CLASS_NAME.exp file with two entries: _PLUG_ENTRY and _PLUG_VIEW_ENTRY
// (these two defines, each with a leading underscore).
// No quotes or spaces.
#define PLUG_ENTRY           IPlugVerb_Entry
#define PLUG_VIEW_ENTRY      IPlugVerb_ViewEntry
// The same strings, with double quotes.  There's no other way, trust me.
#define PLUG_ENTRY_STR      "IPlugVerb_Entry"
#define PLUG_VIEW_ENTRY_STR "IPlugVerb_ViewEntry"
// This is the exported cocoa view class, some hosts display this string.
// No quotes or spaces.
#define VIEW_CLASS           IPlugVerb_View
#define VIEW_CLASS_STR      "IPlugVerb_View"

//               0xMajrMnBg
#define PLUG_VER 0x00000001

// http://service.steinberg.de/databases/plugin.nsf/plugIn?openForm
// 4 chars, single quotes.
#define PLUG_UNIQUE_ID 'Iplv'
#define PLUG_MFR_ID    'Tale'

#define PLUG_CHANNEL_IO "1-1 2-2" // 1 in 1 out, 2 in 2 out

#define PLUG_LATENCY           0
#define PLUG_IS_INST           0
#define PLUG_DOES_MIDI         0
#define PLUG_DOES_STATE_CHUNKS 0
