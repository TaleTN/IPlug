// Double quotes, spaces OK.
#define PLUG_MFR            "Tale"
#define PLUG_NAME           "IPlug Delay"

// No quotes or spaces.
#define PLUG_CLASS_NAME      IPlugDelay

// OSX crap.
// - Manually edit the info.plist file to set the CFBundleIdentifier to the either the string 
// "BUNDLE_DOMAIN.audiounit.BUNDLE_NAME" or "BUNDLE_DOMAIN.vst.BUNDLE_NAME".
// Double quotes, no spaces. BUNDLE_DOMAIN must contain only alphanumeric
// (A-Z,a-z,0-9), hyphen (-), and period (.) characters.
#define BUNDLE_DOMAIN       "com.TaleTN"
#define BUNDLE_NAME         "IPlugDelay"
// - Manually create a PLUG_CLASS_NAME.exp file with two entries: _PLUG_ENTRY and _PLUG_VIEW_ENTRY
// (these two defines, each with a leading underscore).
// No quotes or spaces.
#define PLUG_ENTRY           IPlugDelay_Entry
#define PLUG_VIEW_ENTRY      IPlugDelay_ViewEntry
// The same strings, with double quotes.  There's no other way, trust me.
#define PLUG_ENTRY_STR      "IPlugDelay_Entry"
#define PLUG_VIEW_ENTRY_STR "IPlugDelay_ViewEntry"
// This is the exported cocoa view class, some hosts display this string.
// No quotes or spaces.
#define VIEW_CLASS           IPlugDelay_View
#define VIEW_CLASS_STR      "IPlugDelay_View"

//               0xMajrMnBg
#define PLUG_VER 0x00010000

// http://service.steinberg.de/databases/plugin.nsf/plugIn?openForm
// 4 chars, single quotes.
#define PLUG_UNIQUE_ID 'Ipdy'
#define PLUG_MFR_ID    'Tale'

#define PLUG_CHANNEL_IO "1-1 2-2"

#define PLUG_LATENCY           0
#define PLUG_IS_INST           0
#define PLUG_DOES_MIDI         0
#define PLUG_DOES_STATE_CHUNKS 0
