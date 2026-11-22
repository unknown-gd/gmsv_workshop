#pragma once

#include <cstddef>
#include <cstdint>

// Steamworks packs callback structs to 4 bytes on Linux and 8 on Windows.
// These layouts match STEAMUGC_INTERFACE_VERSION021 (GMod's libsteam_api.so).
#if defined(_WIN32)
#pragma pack(push, 8)
#else
#pragma pack(push, 4)
#endif

inline constexpr uint64_t kUgcQueryHandleInvalid = ~uint64_t{0};
inline constexpr int kEResultOK = 1;
inline constexpr int kEResultIOFailure = 37;
inline constexpr int kSteamUGCQueryCompleted = 3401;
inline constexpr uint32_t kItemStateInstalled = 4;

inline constexpr std::size_t kUgcTitleMax = 129;
inline constexpr std::size_t kUgcDescriptionMax = 8000;
inline constexpr std::size_t kUgcTagsMax = 1025;
inline constexpr std::size_t kUgcFilenameMax = 260;
inline constexpr std::size_t kUgcUrlMax = 256;

struct SteamUGCDetails_t {
    uint64_t m_nPublishedFileId;
    int32_t m_eResult;
    int32_t m_eFileType;
    uint32_t m_nCreatorAppID;
    uint32_t m_nConsumerAppID;
    char m_rgchTitle[kUgcTitleMax];
    char m_rgchDescription[kUgcDescriptionMax];
    uint64_t m_ulSteamIDOwner;
    uint32_t m_rtimeCreated;
    uint32_t m_rtimeUpdated;
    uint32_t m_rtimeAddedToUserList;
    int32_t m_eVisibility;
    bool m_bBanned;
    bool m_bAcceptedForUse;
    bool m_bTagsTruncated;
    char m_rgchTags[kUgcTagsMax];
    uint64_t m_hFile;
    uint64_t m_hPreviewFile;
    char m_pchFileName[kUgcFilenameMax];
    int32_t m_nFileSize;
    int32_t m_nPreviewFileSize;
    char m_rgchURL[kUgcUrlMax];
    uint32_t m_unVotesUp;
    uint32_t m_unVotesDown;
    float m_flScore;
    uint32_t m_unNumChildren;
    uint64_t m_ulTotalFilesSize;
};

struct SteamUGCQueryCompleted_t {
    uint64_t m_handle;
    int32_t m_eResult;
    uint32_t m_unNumResultsReturned;
    uint32_t m_unTotalMatchingResults;
    bool m_bCachedData;
    char m_rgchNextCursor[kUgcUrlMax];
};

#pragma pack(pop)

static_assert(sizeof(bool) == 1, "Steam bool fields are one byte");
static_assert(sizeof(SteamUGCQueryCompleted_t) == 280, "SteamUGCQueryCompleted_t size drifted");
static_assert(offsetof(SteamUGCQueryCompleted_t, m_eResult) == 8);
static_assert(offsetof(SteamUGCQueryCompleted_t, m_bCachedData) == 20);
static_assert(offsetof(SteamUGCQueryCompleted_t, m_rgchNextCursor) == 21);

#if defined(_WIN32)
static_assert(sizeof(SteamUGCDetails_t) == 9784, "SteamUGCDetails_t size drifted");
static_assert(offsetof(SteamUGCDetails_t, m_ulSteamIDOwner) == 8160);
static_assert(offsetof(SteamUGCDetails_t, m_hFile) == 9216);
static_assert(offsetof(SteamUGCDetails_t, m_ulTotalFilesSize) == 9776);
#else
static_assert(sizeof(SteamUGCDetails_t) == 9772, "SteamUGCDetails_t size drifted");
static_assert(offsetof(SteamUGCDetails_t, m_ulSteamIDOwner) == 8156);
static_assert(offsetof(SteamUGCDetails_t, m_hFile) == 9208);
static_assert(offsetof(SteamUGCDetails_t, m_rgchURL) == 9492);
static_assert(offsetof(SteamUGCDetails_t, m_ulTotalFilesSize) == 9764);
#endif
