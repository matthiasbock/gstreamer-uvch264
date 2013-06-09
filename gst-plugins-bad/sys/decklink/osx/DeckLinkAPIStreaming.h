/* -LICENSE-START-
** Copyright (c) 2011 Blackmagic Design
**
** Permission is hereby granted, free of charge, to any person or organization
** obtaining a copy of the software and accompanying documentation covered by
** this license (the "Software") to use, reproduce, display, distribute,
** execute, and transmit the Software, and to prepare derivative works of the
** Software, and to permit third-parties to whom the Software is furnished to
** do so, all subject to the following:
** 
** The copyright notices in the Software and this entire statement, including
** the above license grant, this restriction and the following disclaimer,
** must be included in all copies of the Software, in whole or in part, and
** all derivative works of the Software, unless such copies or derivative
** works are solely in the form of machine-executable object code generated by
** a source language processor.
** 
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
** SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
** FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
** ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
** DEALINGS IN THE SOFTWARE.
** -LICENSE-END-
*/

#ifndef BMD_DECKLINKAPISTREAMING_H
#define BMD_DECKLINKAPISTREAMING_H

// Type Declarations


// Interface ID Declarations

#define IID_IBMDStreamingDeviceNotificationCallback      /* F9531D64-3305-4B29-A387-7F74BB0D0E84 */ (REFIID){0xF9,0x53,0x1D,0x64,0x33,0x05,0x4B,0x29,0xA3,0x87,0x7F,0x74,0xBB,0x0D,0x0E,0x84}
#define IID_IBMDStreamingH264InputCallback               /* 823C475F-55AE-46F9-890C-537CC5CEDCCA */ (REFIID){0x82,0x3C,0x47,0x5F,0x55,0xAE,0x46,0xF9,0x89,0x0C,0x53,0x7C,0xC5,0xCE,0xDC,0xCA}
#define IID_IBMDStreamingDiscovery                       /* 2C837444-F989-4D87-901A-47C8A36D096D */ (REFIID){0x2C,0x83,0x74,0x44,0xF9,0x89,0x4D,0x87,0x90,0x1A,0x47,0xC8,0xA3,0x6D,0x09,0x6D}
#define IID_IBMDStreamingVideoEncodingMode               /* 1AB8035B-CD13-458D-B6DF-5E8F7C2141D9 */ (REFIID){0x1A,0xB8,0x03,0x5B,0xCD,0x13,0x45,0x8D,0xB6,0xDF,0x5E,0x8F,0x7C,0x21,0x41,0xD9}
#define IID_IBMDStreamingMutableVideoEncodingMode        /* 19BF7D90-1E0A-400D-B2C6-FFC4E78AD49D */ (REFIID){0x19,0xBF,0x7D,0x90,0x1E,0x0A,0x40,0x0D,0xB2,0xC6,0xFF,0xC4,0xE7,0x8A,0xD4,0x9D}
#define IID_IBMDStreamingVideoEncodingModePresetIterator /* 7AC731A3-C950-4AD0-804A-8377AA51C6C4 */ (REFIID){0x7A,0xC7,0x31,0xA3,0xC9,0x50,0x4A,0xD0,0x80,0x4A,0x83,0x77,0xAA,0x51,0xC6,0xC4}
#define IID_IBMDStreamingDeviceInput                     /* 24B6B6EC-1727-44BB-9818-34FF086ACF98 */ (REFIID){0x24,0xB6,0xB6,0xEC,0x17,0x27,0x44,0xBB,0x98,0x18,0x34,0xFF,0x08,0x6A,0xCF,0x98}
#define IID_IBMDStreamingH264NALPacket                   /* E260E955-14BE-4395-9775-9F02CC0A9D89 */ (REFIID){0xE2,0x60,0xE9,0x55,0x14,0xBE,0x43,0x95,0x97,0x75,0x9F,0x02,0xCC,0x0A,0x9D,0x89}
#define IID_IBMDStreamingAudioPacket                     /* D9EB5902-1AD2-43F4-9E2C-3CFA50B5EE19 */ (REFIID){0xD9,0xEB,0x59,0x02,0x1A,0xD2,0x43,0xF4,0x9E,0x2C,0x3C,0xFA,0x50,0xB5,0xEE,0x19}
#define IID_IBMDStreamingMPEG2TSPacket                   /* 91810D1C-4FB3-4AAA-AE56-FA301D3DFA4C */ (REFIID){0x91,0x81,0x0D,0x1C,0x4F,0xB3,0x4A,0xAA,0xAE,0x56,0xFA,0x30,0x1D,0x3D,0xFA,0x4C}
#define IID_IBMDStreamingH264NALParser                   /* 5867F18C-5BFA-4CCC-B2A7-9DFD140417D2 */ (REFIID){0x58,0x67,0xF1,0x8C,0x5B,0xFA,0x4C,0xCC,0xB2,0xA7,0x9D,0xFD,0x14,0x04,0x17,0xD2}

/* Enum BMDStreamingDeviceMode - Device modes */

typedef uint32_t BMDStreamingDeviceMode;
enum _BMDStreamingDeviceMode {
    bmdStreamingDeviceIdle                                       = 'idle',
    bmdStreamingDeviceEncoding                                   = 'enco',
    bmdStreamingDeviceStopping                                   = 'stop',
    bmdStreamingDeviceUnknown                                    = 'munk'
};

/* Enum BMDStreamingEncodingFrameRate - Encoded frame rates */

typedef uint32_t BMDStreamingEncodingFrameRate;
enum _BMDStreamingEncodingFrameRate {

    /* Interlaced rates */

    bmdStreamingEncodedFrameRate50i                              = 'e50i',
    bmdStreamingEncodedFrameRate5994i                            = 'e59i',
    bmdStreamingEncodedFrameRate60i                              = 'e60i',

    /* Progressive rates */

    bmdStreamingEncodedFrameRate2398p                            = 'e23p',
    bmdStreamingEncodedFrameRate24p                              = 'e24p',
    bmdStreamingEncodedFrameRate25p                              = 'e25p',
    bmdStreamingEncodedFrameRate2997p                            = 'e29p',
    bmdStreamingEncodedFrameRate30p                              = 'e30p',
    bmdStreamingEncodedFrameRate50p                              = 'e50p',
    bmdStreamingEncodedFrameRate5994p                            = 'e59p',
    bmdStreamingEncodedFrameRate60p                              = 'e60p'
};

/* Enum BMDStreamingEncodingSupport - Output encoding mode supported flag */

typedef uint32_t BMDStreamingEncodingSupport;
enum _BMDStreamingEncodingSupport {
    bmdStreamingEncodingModeNotSupported                         = 0,
    bmdStreamingEncodingModeSupported,                          
    bmdStreamingEncodingModeSupportedWithChanges                
};

/* Enum BMDStreamingVideoCodec - Video codecs */

typedef uint32_t BMDStreamingVideoCodec;
enum _BMDStreamingVideoCodec {
    bmdStreamingVideoCodecH264                                   = 'H264'
};

/* Enum BMDStreamingH264Profile - H264 encoding profile */

typedef uint32_t BMDStreamingH264Profile;
enum _BMDStreamingH264Profile {
    bmdStreamingH264ProfileHigh                                  = 'high',
    bmdStreamingH264ProfileMain                                  = 'main',
    bmdStreamingH264ProfileBaseline                              = 'base'
};

/* Enum BMDStreamingH264Level - H264 encoding level */

typedef uint32_t BMDStreamingH264Level;
enum _BMDStreamingH264Level {
    bmdStreamingH264Level12                                      = 'lv12',
    bmdStreamingH264Level13                                      = 'lv13',
    bmdStreamingH264Level2                                       = 'lv2 ',
    bmdStreamingH264Level21                                      = 'lv21',
    bmdStreamingH264Level22                                      = 'lv22',
    bmdStreamingH264Level3                                       = 'lv3 ',
    bmdStreamingH264Level31                                      = 'lv31',
    bmdStreamingH264Level32                                      = 'lv32',
    bmdStreamingH264Level4                                       = 'lv4 ',
    bmdStreamingH264Level41                                      = 'lv41',
    bmdStreamingH264Level42                                      = 'lv42'
};

/* Enum BMDStreamingH264EntropyCoding - H264 entropy coding */

typedef uint32_t BMDStreamingH264EntropyCoding;
enum _BMDStreamingH264EntropyCoding {
    bmdStreamingH264EntropyCodingCAVLC                           = 'EVLC',
    bmdStreamingH264EntropyCodingCABAC                           = 'EBAC'
};

/* Enum BMDStreamingAudioCodec - Audio codecs */

typedef uint32_t BMDStreamingAudioCodec;
enum _BMDStreamingAudioCodec {
    bmdStreamingAudioCodecAAC                                    = 'AAC '
};

/* Enum BMDStreamingEncodingModePropertyID - Encoding mode properties */

typedef uint32_t BMDStreamingEncodingModePropertyID;
enum _BMDStreamingEncodingModePropertyID {

    /* Integers, Video Properties */

    bmdStreamingEncodingPropertyVideoFrameRate                   = 'vfrt',	// Uses values of type BMDStreamingEncodingFrameRate
    bmdStreamingEncodingPropertyVideoBitRateKbps                 = 'vbrt',

    /* Integers, H264 Properties */

    bmdStreamingEncodingPropertyH264Profile                      = 'hprf',
    bmdStreamingEncodingPropertyH264Level                        = 'hlvl',
    bmdStreamingEncodingPropertyH264EntropyCoding                = 'hent',

    /* Flags, H264 Properties */

    bmdStreamingEncodingPropertyH264HasBFrames                   = 'hBfr',

    /* Integers, Audio Properties */

    bmdStreamingEncodingPropertyAudioCodec                       = 'acdc',
    bmdStreamingEncodingPropertyAudioSampleRate                  = 'asrt',
    bmdStreamingEncodingPropertyAudioChannelCount                = 'achc',
    bmdStreamingEncodingPropertyAudioBitRateKbps                 = 'abrt'
};

// Forward Declarations

class IBMDStreamingDeviceNotificationCallback;
class IBMDStreamingH264InputCallback;
class IBMDStreamingDiscovery;
class IBMDStreamingVideoEncodingMode;
class IBMDStreamingMutableVideoEncodingMode;
class IBMDStreamingVideoEncodingModePresetIterator;
class IBMDStreamingDeviceInput;
class IBMDStreamingH264NALPacket;
class IBMDStreamingAudioPacket;
class IBMDStreamingMPEG2TSPacket;
class IBMDStreamingH264NALParser;

/* Interface IBMDStreamingDeviceNotificationCallback - Device notification callbacks. */

class IBMDStreamingDeviceNotificationCallback : public IUnknown
{
public:
    virtual HRESULT StreamingDeviceArrived (/* in */ IDeckLink* device) = 0;
    virtual HRESULT StreamingDeviceRemoved (/* in */ IDeckLink* device) = 0;
    virtual HRESULT StreamingDeviceModeChanged (/* in */ IDeckLink* device, /* in */ BMDStreamingDeviceMode mode) = 0;

protected:
    virtual ~IBMDStreamingDeviceNotificationCallback () {}; // call Release method to drop reference count
};

/* Interface IBMDStreamingH264InputCallback - H264 input callbacks. */

class IBMDStreamingH264InputCallback : public IUnknown
{
public:
    virtual HRESULT H264NALPacketArrived (/* in */ IBMDStreamingH264NALPacket* nalPacket) = 0;
    virtual HRESULT H264AudioPacketArrived (/* in */ IBMDStreamingAudioPacket* audioPacket) = 0;
    virtual HRESULT MPEG2TSPacketArrived (/* in */ IBMDStreamingMPEG2TSPacket* tsPacket) = 0;
    virtual HRESULT H264VideoInputConnectorScanningChanged (void) = 0;
    virtual HRESULT H264VideoInputConnectorChanged (void) = 0;
    virtual HRESULT H264VideoInputModeChanged (void) = 0;

protected:
    virtual ~IBMDStreamingH264InputCallback () {}; // call Release method to drop reference count
};

/* Interface IBMDStreamingDiscovery - Installs device notifications */

class IBMDStreamingDiscovery : public IUnknown
{
public:
    virtual HRESULT InstallDeviceNotifications (/* in */ IBMDStreamingDeviceNotificationCallback* theCallback) = 0;
    virtual HRESULT UninstallDeviceNotifications (void) = 0;

protected:
    virtual ~IBMDStreamingDiscovery () {}; // call Release method to drop reference count
};

/* Interface IBMDStreamingVideoEncodingMode - Represents an encoded video mode. */

class IBMDStreamingVideoEncodingMode : public IUnknown
{
public:
    virtual HRESULT GetName (/* out */ CFStringRef *name) = 0;
    virtual unsigned int GetPresetID (void) = 0;
    virtual unsigned int GetSourcePositionX (void) = 0;
    virtual unsigned int GetSourcePositionY (void) = 0;
    virtual unsigned int GetSourceWidth (void) = 0;
    virtual unsigned int GetSourceHeight (void) = 0;
    virtual unsigned int GetDestWidth (void) = 0;
    virtual unsigned int GetDestHeight (void) = 0;
    virtual HRESULT GetFlag (/* in */ BMDStreamingEncodingModePropertyID cfgID, /* out */ bool* value) = 0;
    virtual HRESULT GetInt (/* in */ BMDStreamingEncodingModePropertyID cfgID, /* out */ int64_t* value) = 0;
    virtual HRESULT GetFloat (/* in */ BMDStreamingEncodingModePropertyID cfgID, /* out */ double* value) = 0;
    virtual HRESULT GetString (/* in */ BMDStreamingEncodingModePropertyID cfgID, /* out */ CFStringRef *value) = 0;
    virtual HRESULT CreateMutableVideoEncodingMode (/* out */ IBMDStreamingMutableVideoEncodingMode** newEncodingMode) = 0; // Creates a mutable copy of the encoding mode

protected:
    virtual ~IBMDStreamingVideoEncodingMode () {}; // call Release method to drop reference count
};

/* Interface IBMDStreamingMutableVideoEncodingMode - Represents a mutable encoded video mode. */

class IBMDStreamingMutableVideoEncodingMode : public IBMDStreamingVideoEncodingMode
{
public:
    virtual HRESULT SetSourceRect (/* in */ uint32_t posX, /* in */ uint32_t posY, /* in */ uint32_t width, /* in */ uint32_t height) = 0;
    virtual HRESULT SetDestSize (/* in */ uint32_t width, /* in */ uint32_t height) = 0;
    virtual HRESULT SetFlag (/* in */ BMDStreamingEncodingModePropertyID cfgID, /* in */ bool value) = 0;
    virtual HRESULT SetInt (/* in */ BMDStreamingEncodingModePropertyID cfgID, /* in */ int64_t value) = 0;
    virtual HRESULT SetFloat (/* in */ BMDStreamingEncodingModePropertyID cfgID, /* in */ double value) = 0;
    virtual HRESULT SetString (/* in */ BMDStreamingEncodingModePropertyID cfgID, /* in */ CFStringRef value) = 0;

protected:
    virtual ~IBMDStreamingMutableVideoEncodingMode () {}; // call Release method to drop reference count
};

/* Interface IBMDStreamingVideoEncodingModePresetIterator - Enumerates encoding mode presets */

class IBMDStreamingVideoEncodingModePresetIterator : public IUnknown
{
public:
    virtual HRESULT Next (/* out */ IBMDStreamingVideoEncodingMode** videoEncodingMode) = 0;

protected:
    virtual ~IBMDStreamingVideoEncodingModePresetIterator () {}; // call Release method to drop reference count
};

/* Interface IBMDStreamingDeviceInput - Created by QueryInterface from IDeckLink */

class IBMDStreamingDeviceInput : public IUnknown
{
public:

    /* Input modes */

    virtual HRESULT DoesSupportVideoInputMode (/* in */ BMDDisplayMode inputMode, /* out */ bool* result) = 0;
    virtual HRESULT GetVideoInputModeIterator (/* out */ IDeckLinkDisplayModeIterator** iterator) = 0;
    virtual HRESULT SetVideoInputMode (/* in */ BMDDisplayMode inputMode) = 0;
    virtual HRESULT GetCurrentDetectedVideoInputMode (/* out */ BMDDisplayMode* detectedMode) = 0;

    /* Capture modes */

    virtual HRESULT GetVideoEncodingMode (/* out */ IBMDStreamingVideoEncodingMode** encodingMode) = 0;
    virtual HRESULT GetVideoEncodingModePresetIterator (/* in */ BMDDisplayMode inputMode, /* out */ IBMDStreamingVideoEncodingModePresetIterator** iterator) = 0;
    virtual HRESULT DoesSupportVideoEncodingMode (/* in */ BMDDisplayMode inputMode, /* in */ IBMDStreamingVideoEncodingMode* encodingMode, /* out */ BMDStreamingEncodingSupport* result, /* out */ IBMDStreamingVideoEncodingMode** changedEncodingMode) = 0;
    virtual HRESULT SetVideoEncodingMode (/* in */ IBMDStreamingVideoEncodingMode* encodingMode) = 0;

    /* Input control */

    virtual HRESULT StartCapture (void) = 0;
    virtual HRESULT StopCapture (void) = 0;
    virtual HRESULT SetCallback (/* in */ IUnknown* theCallback) = 0;

protected:
    virtual ~IBMDStreamingDeviceInput () {}; // call Release method to drop reference count
};

/* Interface IBMDStreamingH264NALPacket - Represent an H.264 NAL packet */

class IBMDStreamingH264NALPacket : public IUnknown
{
public:
    virtual long GetPayloadSize (void) = 0;
    virtual HRESULT GetBytes (/* out */ void** buffer) = 0;
    virtual HRESULT GetBytesWithSizePrefix (/* out */ void** buffer) = 0; // Contains a 32-bit unsigned big endian size prefix
    virtual HRESULT GetDisplayTime (/* in */ uint64_t requestedTimeScale, /* out */ uint64_t* displayTime) = 0;
    virtual HRESULT GetPacketIndex (/* out */ uint32_t* packetIndex) = 0;

protected:
    virtual ~IBMDStreamingH264NALPacket () {}; // call Release method to drop reference count
};

/* Interface IBMDStreamingAudioPacket - Represents a chunk of audio data */

class IBMDStreamingAudioPacket : public IUnknown
{
public:
    virtual BMDStreamingAudioCodec GetCodec (void) = 0;
    virtual long GetPayloadSize (void) = 0;
    virtual HRESULT GetBytes (/* out */ void** buffer) = 0;
    virtual HRESULT GetPlayTime (/* in */ uint64_t requestedTimeScale, /* out */ uint64_t* playTime) = 0;
    virtual HRESULT GetPacketIndex (/* out */ uint32_t* packetIndex) = 0;

protected:
    virtual ~IBMDStreamingAudioPacket () {}; // call Release method to drop reference count
};

/* Interface IBMDStreamingMPEG2TSPacket - Represent an MPEG2 Transport Stream packet */

class IBMDStreamingMPEG2TSPacket : public IUnknown
{
public:
    virtual long GetPayloadSize (void) = 0;
    virtual HRESULT GetBytes (/* out */ void** buffer) = 0;

protected:
    virtual ~IBMDStreamingMPEG2TSPacket () {}; // call Release method to drop reference count
};

/* Interface IBMDStreamingH264NALParser - For basic NAL parsing */

class IBMDStreamingH264NALParser : public IUnknown
{
public:
    virtual HRESULT IsNALSequenceParameterSet (/* in */ IBMDStreamingH264NALPacket* nal) = 0;
    virtual HRESULT IsNALPictureParameterSet (/* in */ IBMDStreamingH264NALPacket* nal) = 0;
    virtual HRESULT GetProfileAndLevelFromSPS (/* in */ IBMDStreamingH264NALPacket* nal, /* out */ uint32_t* profileIdc, /* out */ uint32_t* profileCompatability, /* out */ uint32_t* levelIdc) = 0;

protected:
    virtual ~IBMDStreamingH264NALParser () {}; // call Release method to drop reference count
};

/* Functions */

extern "C" {

    IBMDStreamingDiscovery* CreateBMDStreamingDiscoveryInstance (void);
    IBMDStreamingH264NALParser* CreateBMDStreamingH264NALParser (void);

};


#endif /* defined(BMD_DECKLINKAPISTREAMING_H) */
