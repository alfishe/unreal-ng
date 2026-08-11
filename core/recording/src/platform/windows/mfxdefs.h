/// Minimal Intel Media SDK / oneVPL definitions for QuickSync encoding
/// Based on Intel oneVPL API - used for runtime DLL loading without SDK dependency
#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Status codes
typedef int mfxStatus;
#define MFX_ERR_NONE                     0
#define MFX_ERR_UNKNOWN                 -1
#define MFX_ERR_NULL_PTR                -2
#define MFX_ERR_UNSUPPORTED             -3
#define MFX_ERR_MEMORY_ALLOC            -4
#define MFX_ERR_NOT_ENOUGH_BUFFER       -5
#define MFX_ERR_INVALID_HANDLE          -6
#define MFX_ERR_LOCK_MEMORY             -7
#define MFX_ERR_NOT_INITIALIZED         -8
#define MFX_ERR_NOT_FOUND               -9
#define MFX_ERR_MORE_DATA              -10
#define MFX_ERR_MORE_SURFACE           -11
#define MFX_ERR_ABORTED                -12
#define MFX_ERR_DEVICE_LOST            -13
#define MFX_ERR_INCOMPATIBLE_VIDEO_PARAM -14
#define MFX_ERR_INVALID_VIDEO_PARAM    -15
#define MFX_ERR_UNDEFINED_BEHAVIOR     -16
#define MFX_ERR_DEVICE_FAILED          -17
#define MFX_ERR_MORE_BITSTREAM         -18
#define MFX_ERR_GPU_HANG               -21
#define MFX_ERR_REALLOC_SURFACE        -22
#define MFX_ERR_RESOURCE_MAPPED        -23
#define MFX_ERR_NOT_IMPLEMENTED        -24

#define MFX_WRN_IN_EXECUTION            1
#define MFX_WRN_DEVICE_BUSY             2
#define MFX_WRN_VIDEO_PARAM_CHANGED     3
#define MFX_WRN_PARTIAL_ACCELERATION    4
#define MFX_WRN_INCOMPATIBLE_VIDEO_PARAM 5
#define MFX_WRN_VALUE_NOT_CHANGED       6
#define MFX_WRN_OUT_OF_RANGE            7
#define MFX_WRN_FILTER_SKIPPED          10
#define MFX_WRN_ALLOC_TIMEOUT_EXPIRED   13

// Implementation type
typedef int mfxIMPL;
#define MFX_IMPL_AUTO                   0x0000
#define MFX_IMPL_SOFTWARE               0x0001
#define MFX_IMPL_HARDWARE               0x0002
#define MFX_IMPL_AUTO_ANY               0x0003
#define MFX_IMPL_HARDWARE_ANY           0x0004
#define MFX_IMPL_HARDWARE2              0x0005
#define MFX_IMPL_HARDWARE3              0x0006
#define MFX_IMPL_HARDWARE4              0x0007
#define MFX_IMPL_VIA_D3D9               0x0100
#define MFX_IMPL_VIA_D3D11              0x0200

// Handle type
typedef void* mfxSession;
typedef void* mfxHDL;

typedef uint16_t mfxU16;
typedef uint32_t mfxU32;
typedef uint64_t mfxU64;
typedef int16_t mfxI16;
typedef int32_t mfxI32;
typedef int64_t mfxI64;
typedef uint8_t mfxU8;
typedef int8_t mfxI8;
typedef double mfxF64;

// Version
typedef union {
    struct {
        mfxU16 Minor;
        mfxU16 Major;
    };
    mfxU32 Version;
} mfxVersion;

// Priority
typedef enum {
    MFX_PRIORITY_LOW = 0,
    MFX_PRIORITY_NORMAL = 1,
    MFX_PRIORITY_HIGH = 2
} mfxPriority;

// Codec ID
#define MFX_CODEC_AVC   0x20435641  // 'AVC '
#define MFX_CODEC_HEVC  0x43564548  // 'HEVC'
#define MFX_CODEC_AV1   0x31305641  // 'AV1\0'

// FourCC
#define MFX_FOURCC_NV12         0x3231564E  // 'NV12'
#define MFX_FOURCC_RGB4         0x00000100
#define MFX_FOURCC_YUY2         0x32595559
#define MFX_FOURCC_P010         0x30313050
#define MFX_FOURCC_BGRA         0x41524742

// Picture structure
#define MFX_PICSTRUCT_PROGRESSIVE    0x0001
#define MFX_PICSTRUCT_FIELD_TFF      0x0002
#define MFX_PICSTRUCT_FIELD_BFF      0x0004

// Chroma format
#define MFX_CHROMAFORMAT_YUV420      1
#define MFX_CHROMAFORMAT_YUV422      2
#define MFX_CHROMAFORMAT_YUV444      3

// Rate control method
#define MFX_RATECONTROL_CBR          1
#define MFX_RATECONTROL_VBR          2
#define MFX_RATECONTROL_CQP          3
#define MFX_RATECONTROL_AVBR         4
#define MFX_RATECONTROL_ICQ          9
#define MFX_RATECONTROL_VCM         10
#define MFX_RATECONTROL_QVBR        14

// Target usage (quality/speed tradeoff)
#define MFX_TARGETUSAGE_1            1  // Best quality
#define MFX_TARGETUSAGE_2            2
#define MFX_TARGETUSAGE_3            3
#define MFX_TARGETUSAGE_4            4  // Balanced
#define MFX_TARGETUSAGE_5            5
#define MFX_TARGETUSAGE_6            6
#define MFX_TARGETUSAGE_7            7  // Best speed
#define MFX_TARGETUSAGE_BEST_QUALITY MFX_TARGETUSAGE_1
#define MFX_TARGETUSAGE_BALANCED     MFX_TARGETUSAGE_4
#define MFX_TARGETUSAGE_BEST_SPEED   MFX_TARGETUSAGE_7

// Profile
#define MFX_PROFILE_UNKNOWN          0
#define MFX_PROFILE_AVC_BASELINE     66
#define MFX_PROFILE_AVC_MAIN         77
#define MFX_PROFILE_AVC_HIGH        100
#define MFX_PROFILE_HEVC_MAIN        1
#define MFX_PROFILE_HEVC_MAIN10      2

// Level
#define MFX_LEVEL_UNKNOWN            0
#define MFX_LEVEL_AVC_41           41
#define MFX_LEVEL_AVC_51           51
#define MFX_LEVEL_HEVC_41          123  // 4.1 * 30
#define MFX_LEVEL_HEVC_51          153  // 5.1 * 30

// GOP structure
#define MFX_GOP_CLOSED              0x0001
#define MFX_GOP_STRICT              0x0002

// B-frame reference type
#define MFX_B_REF_OFF               0
#define MFX_B_REF_PYRAMID           1

// Memory type
#define MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET  0x0020
#define MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET 0x0040
#define MFX_MEMTYPE_SYSTEM_MEMORY    0x0001
#define MFX_MEMTYPE_FROM_ENCODE      0x0200
#define MFX_MEMTYPE_D3D11_TEXTURE    0x0400

// IO pattern
#define MFX_IOPATTERN_IN_VIDEO_MEMORY   0x0001
#define MFX_IOPATTERN_IN_SYSTEM_MEMORY  0x0002
#define MFX_IOPATTERN_OUT_VIDEO_MEMORY  0x0010
#define MFX_IOPATTERN_OUT_SYSTEM_MEMORY 0x0020

// Frame info
typedef struct {
    mfxU32 reserved[4];
    mfxU16 reserved4;
    mfxU16 BitDepthLuma;
    mfxU16 BitDepthChroma;
    mfxU16 Shift;

    mfxU16 FrameRateExtN;
    mfxU16 FrameRateExtD;
    mfxU16 reserved3;
    mfxU16 AspectRatioW;
    mfxU16 AspectRatioH;

    mfxU16 PicStruct;
    mfxU16 ChromaFormat;
    mfxU16 reserved2;
    mfxU16 Width;
    mfxU16 Height;

    mfxU16 CropX;
    mfxU16 CropY;
    mfxU16 CropW;
    mfxU16 CropH;

    mfxU32 FrameId;
    mfxU32 FourCC;
} mfxFrameInfo;

// Frame data (pointers to actual pixel data)
typedef struct {
    union {
        mfxU8* Y;
        mfxU8* R;
    };
    union {
        mfxU8* UV;
        mfxU8* G;
    };
    mfxU8* B;
    mfxU8* A;
    mfxU8* Cr;
    mfxU8* Cb;
    mfxU8* reserved1;

    mfxU16 Pitch;
    mfxU16 reserved2[7];
    mfxU64 TimeStamp;
    mfxU32 FrameOrder;
    mfxU16 Locked;
    mfxU16 reserved3;
    mfxHDL MemId;  // D3D11 texture handle
    mfxU32 reserved4[4];
} mfxFrameData;

// Frame surface
typedef struct {
    mfxU32 reserved[4];
    mfxFrameInfo Info;
    mfxFrameData Data;
} mfxFrameSurface1;

// Bitstream buffer
typedef struct {
    mfxU32 reserved[4];
    mfxI64 DecodeTimeStamp;
    mfxU64 TimeStamp;
    mfxU8* Data;
    mfxU32 DataOffset;
    mfxU32 DataLength;
    mfxU32 MaxLength;
    mfxU16 PicStruct;
    mfxU16 FrameType;
    mfxU16 DataFlag;
    mfxU16 reserved2;
    mfxU32 reserved3[8];
} mfxBitstream;

// Encode control
typedef struct {
    mfxU32 reserved[4];
    mfxU16 reserved1;
    mfxU16 MfxNalUnitType;
    mfxU16 SkipFrame;
    mfxU16 QP;
    mfxU16 FrameType;
    mfxU16 NumExtParam;
    mfxU16 NumPayload;
    mfxU16 reserved2;
    void** ExtParam;
    void** Payload;
} mfxEncodeCtrl;

// Sync point
typedef void* mfxSyncPoint;

// Coding option
typedef struct {
    mfxU32 reserved[4];
    mfxU16 reserved1;
    mfxU16 RateDistortionOpt;
    mfxU16 MECostType;
    mfxU16 MESearchType;
    mfxI16 MVSearchWindow[2];
    mfxU16 EndOfSequence;
    mfxU16 FramePicture;

    mfxU16 CAVLC;
    mfxU16 reserved2[2];
    mfxU16 RecoveryPointSEI;
    mfxU16 ViewOutput;
    mfxU16 NalHrdConformance;
    mfxU16 SingleSeiNalUnit;
    mfxU16 VuiVclHrdParameters;

    mfxU16 RefPicListReordering;
    mfxU16 ResetRefList;
    mfxU16 RefPicMarkRep;
    mfxU16 FieldOutput;

    mfxU16 IntraPredBlockSize;
    mfxU16 InterPredBlockSize;
    mfxU16 MVPrecision;
    mfxU16 MaxDecFrameBuffering;

    mfxU16 AUDelimiter;
    mfxU16 EndOfStream;
    mfxU16 PicTimingSEI;
    mfxU16 VuiNalHrdParameters;
} mfxCodingOption;

// Coding option 2
typedef struct {
    mfxU32 Header;
    mfxU32 reserved1[2];

    mfxU16 IntRefType;
    mfxU16 IntRefCycleSize;
    mfxI16 IntRefQPDelta;

    mfxU32 MaxFrameSize;
    mfxU32 MaxSliceSize;

    mfxU16 BitrateLimit;
    mfxU16 MBBRC;
    mfxU16 ExtBRC;
    mfxU16 LookAheadDepth;
    mfxU16 Trellis;
    mfxU16 RepeatPPS;
    mfxU16 BRefType;
    mfxU16 AdaptiveI;
    mfxU16 AdaptiveB;
    mfxU16 LookAheadDS;
    mfxU16 NumMbPerSlice;
    mfxU16 SkipFrame;
    mfxU8 MinQPI;
    mfxU8 MaxQPI;
    mfxU8 MinQPP;
    mfxU8 MaxQPP;
    mfxU8 MinQPB;
    mfxU8 MaxQPB;
    mfxU16 FixedFrameRate;
    mfxU16 DisableDeblockingIdc;
    mfxU16 DisableVUI;
    mfxU16 BufferingPeriodSEI;
    mfxU16 EnableMAD;
    mfxU16 UseRawRef;
} mfxCodingOption2;

// Video param
typedef struct {
    mfxU32 reserved[3];
    mfxU16 reserved3;
    mfxU16 AsyncDepth;

    // mfxInfoMFX
    struct {
        mfxU32 reserved[7];
        mfxU16 LowPower;
        mfxU16 BRCParamMultiplier;
        mfxFrameInfo FrameInfo;
        mfxU32 CodecId;
        mfxU16 CodecProfile;
        mfxU16 CodecLevel;
        mfxU16 NumThread;

        union {
            // Encode specific
            struct {
                mfxU16 TargetUsage;
                mfxU16 GopPicSize;
                mfxU16 GopRefDist;
                mfxU16 GopOptFlag;
                mfxU16 IdrInterval;

                mfxU16 RateControlMethod;
                union {
                    mfxU16 InitialDelayInKB;
                    mfxU16 QPI;
                };
                mfxU16 BufferSizeInKB;
                union {
                    mfxU16 TargetKbps;
                    mfxU16 QPP;
                };
                union {
                    mfxU16 MaxKbps;
                    mfxU16 QPB;
                };

                mfxU16 NumSlice;
                mfxU16 NumRefFrame;
                mfxU16 EncodedOrder;
            };
        };
    } mfx;

    mfxU16 IOPattern;
    mfxU16 reserved4[3];
    mfxU16 NumExtParam;
    mfxU16 reserved5;
    void** ExtParam;
} mfxVideoParam;

// Extended buffer header
typedef struct {
    mfxU32 BufferId;
    mfxU32 BufferSz;
} mfxExtBuffer;

// Frame allocator request
typedef struct {
    mfxU32 reserved[4];
    mfxFrameInfo Info;
    mfxU16 Type;
    mfxU16 NumFrameMin;
    mfxU16 NumFrameSuggested;
    mfxU16 reserved2;
} mfxFrameAllocRequest;

// Frame allocator response
typedef struct {
    mfxU32 reserved[4];
    mfxHDL* mids;
    mfxU16 NumFrameActual;
    mfxU16 reserved2;
} mfxFrameAllocResponse;

// Init param for MFXInitEx
typedef struct {
    mfxIMPL Implementation;
    mfxVersion Version;
    mfxU16 ExternalThreads;
    union {
        mfxU32 reserved[2];
    };
    mfxU16 GPUCopy;
    mfxU16 reserved2[5];
} mfxInitParam;

// Frame type
#define MFX_FRAMETYPE_I     0x0001
#define MFX_FRAMETYPE_P     0x0002
#define MFX_FRAMETYPE_B     0x0004
#define MFX_FRAMETYPE_IDR   0x0020
#define MFX_FRAMETYPE_REF   0x0040

// Extension buffer IDs
#define MFX_EXTBUFF_CODING_OPTION       0x00000001
#define MFX_EXTBUFF_CODING_OPTION2      0x00000002
#define MFX_EXTBUFF_VIDEO_SIGNAL_INFO   0x00000003

// oneVPL loader types
typedef void* mfxLoader;
typedef void* mfxConfig;

#ifdef __cplusplus
}
#endif
