#include "wl_def.h"

int ChunksInFile;
int PMSpriteStart;
int PMSoundStart;

bool PMSoundInfoPagePadded = false;

// holds the whole VSWAP
uint32_t *PMPageData;
size_t PMPageDataSize;

// ChunksInFile+1 pointers to page starts.
// The last pointer points one byte after the last page.
uint8_t **PMPages;

#ifdef WOLF3D_CYD_PORT
#include <esp_system.h>
#include <esp_heap_caps.h>
extern "C" void furi_log_print_format(int, const char *, const char *, ...);
extern "C" void cyd_wolf3d_status(const char *message);
extern "C" uint32_t cyd_perf_micros(void);
extern "C" void cyd_trace_pm_hit(int page);
extern "C" void cyd_trace_pm_miss(int page, uint32_t bytes, uint32_t readUs);
namespace {
#ifndef CYD_WOLF_HOT_PAGE_CACHE
#define CYD_WOLF_HOT_PAGE_CACHE 0
#endif
#ifndef CYD_WOLF_HOT_PAGE_CACHE_SLOTS
#define CYD_WOLF_HOT_PAGE_CACHE_SLOTS 0
#endif
#ifndef CYD_WOLF_HOT_PAGE_CACHE_MAX_BYTES
#define CYD_WOLF_HOT_PAGE_CACHE_MAX_BYTES 0
#endif

constexpr int PM_CACHE_SLOTS = 3;
constexpr int PM_PREALLOC_SLOTS = 3;
constexpr int PM_EMPTY_PAGE_SIZE = 1;
constexpr uint32_t PM_PREALLOC_SIZE = 4096;
constexpr uint32_t PM_MIN_FREE_AFTER_ALLOC = 30000;
constexpr bool PM_ENABLE_STATS_LOG = false;

uint32_t *PMPageOffsets = nullptr;
uint32_t *PMPageSizes = nullptr;
char PMPageFileName[13] = {};
uint32_t PMCacheClock = 0;
uint8_t PMEmptyPage[PM_EMPTY_PAGE_SIZE] = {};
uint32_t PMStatHits = 0;
uint32_t PMStatMisses = 0;
uint32_t PMStatReadUs = 0;
uint32_t PMStatLastLogMs = 0;

struct PMCacheSlot {
    int page = -1;
    uint8_t *data = nullptr;
    uint32_t capacity = 0;
    uint32_t lastUse = 0;
    bool pinned = false;
};

PMCacheSlot PMCache[PM_CACHE_SLOTS];

#if CYD_WOLF_HOT_PAGE_CACHE && CYD_WOLF_HOT_PAGE_CACHE_SLOTS > 0
struct PMHotPageSlot {
    int page = -1;
    uint8_t *data = nullptr;
    uint32_t size = 0;
    uint32_t capacity = 0;
    uint32_t lastUse = 0;
};

PMHotPageSlot PMHotPages[CYD_WOLF_HOT_PAGE_CACHE_SLOTS];

void PM_ClearHotPages()
{
    for(int i = 0; i < CYD_WOLF_HOT_PAGE_CACHE_SLOTS; ++i)
    {
        free(PMHotPages[i].data);
        PMHotPages[i].data = nullptr;
        PMHotPages[i].capacity = 0;
        PMHotPages[i].size = 0;
        PMHotPages[i].page = -1;
        PMHotPages[i].lastUse = 0;
    }
}

uint8_t *PM_FindHotPage(int page)
{
    for(int i = 0; i < CYD_WOLF_HOT_PAGE_CACHE_SLOTS; ++i)
    {
        if(PMHotPages[i].page == page)
        {
            PMHotPages[i].lastUse = ++PMCacheClock;
            return PMHotPages[i].data;
        }
    }
    return nullptr;
}

int PM_ChooseHotPageSlot()
{
    int best = 0;
    for(int i = 0; i < CYD_WOLF_HOT_PAGE_CACHE_SLOTS; ++i)
    {
        if(PMHotPages[i].page < 0) return i;
        if(PMHotPages[i].lastUse < PMHotPages[best].lastUse) best = i;
    }
    return best;
}
#else
void PM_ClearHotPages() {}
uint8_t *PM_FindHotPage(int) { return nullptr; }
#endif

void PM_ClearCache()
{
    PM_ClearHotPages();
    for(int i = 0; i < PM_CACHE_SLOTS; ++i)
    {
        free(PMCache[i].data);
        PMCache[i].data = nullptr;
        PMCache[i].capacity = 0;
        PMCache[i].page = -1;
        PMCache[i].lastUse = 0;
        PMCache[i].pinned = false;
    }
}

void PM_PreallocateCache()
{
    for(int i = 0; i < PM_PREALLOC_SLOTS; ++i)
    {
        if(PMCache[i].data) continue;
        PMCache[i].data = (uint8_t *) malloc(PM_PREALLOC_SIZE);
        if(!PMCache[i].data)
        {
            furi_log_print_format(2, "Wolf3D", "PM prealloc slot %i failed heap %u largest %u",
                i, esp_get_free_heap_size(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
            break;
        }
        PMCache[i].capacity = PM_PREALLOC_SIZE;
        PMCache[i].page = -1;
        PMCache[i].lastUse = 0;
        furi_log_print_format(2, "Wolf3D", "PM prealloc slot %i size %u heap %u largest %u",
            i, PM_PREALLOC_SIZE, esp_get_free_heap_size(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }
}

void PM_LogStats()
{
    if(!PM_ENABLE_STATS_LOG) return;
    uint32_t nowMs = cyd_perf_micros() / 1000;
    if(nowMs - PMStatLastLogMs < 2000) return;

    uint32_t total = PMStatHits + PMStatMisses;
    if(total)
    {
        uint32_t missPct = (PMStatMisses * 100) / total;
        uint32_t avgReadUs = PMStatMisses ? PMStatReadUs / PMStatMisses : 0;
        furi_log_print_format(2, "Wolf3D",
            "PM hits=%u misses=%u miss=%u%% avgread=%u.%03ums heap=%u max=%u",
            PMStatHits, PMStatMisses, missPct,
            avgReadUs / 1000, avgReadUs % 1000,
            esp_get_free_heap_size(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }

    PMStatHits = 0;
    PMStatMisses = 0;
    PMStatReadUs = 0;
    PMStatLastLogMs = nowMs;
}

int PM_ChooseSlot()
{
    int best = -1;
    for(int i = 0; i < PM_CACHE_SLOTS; ++i)
    {
        if(PMCache[i].page < 0) return i;
        if(PMCache[i].pinned) continue;
        if(best < 0 || PMCache[i].lastUse < PMCache[best].lastUse) best = i;
    }
    return best;
}

bool PM_ReadPageData(int page, uint8_t *target, uint32_t size, uint32_t *readUs)
{
    FILE *file = fopen(PMPageFileName, "rb");
    if(!file) CA_CannotOpen(PMPageFileName);
    uint32_t readStartUs = cyd_perf_micros();
    fseek(file, PMPageOffsets[page], SEEK_SET);
    bool ok = fread(target, 1, size, file) == size;
    fclose(file);
    if(readUs) *readUs = cyd_perf_micros() - readStartUs;
    return ok;
}

#if CYD_WOLF_HOT_PAGE_CACHE && CYD_WOLF_HOT_PAGE_CACHE_SLOTS > 0
uint8_t *PM_LoadHotPage(int page, uint32_t size)
{
    if(size == 0 || size > CYD_WOLF_HOT_PAGE_CACHE_MAX_BYTES)
        return nullptr;

    int slotIndex = PM_ChooseHotPageSlot();
    PMHotPageSlot &slot = PMHotPages[slotIndex];
    if(slot.capacity < size)
    {
        uint8_t *newData = (uint8_t *) realloc(slot.data, size);
        if(!newData)
            return nullptr;
        slot.data = newData;
        slot.capacity = size;
    }

    uint32_t readUs = 0;
    if(!PM_ReadPageData(page, slot.data, size, &readUs))
        Quit("Could not read VSWAP hot page %i", page);

    slot.page = page;
    slot.size = size;
    slot.lastUse = ++PMCacheClock;
    PMStatMisses++;
    PMStatReadUs += readUs;
    cyd_trace_pm_miss(page, size, readUs);
    return slot.data;
}
#else
uint8_t *PM_LoadHotPage(int, uint32_t) { return nullptr; }
#endif
}
#endif

void PM_Startup()
{
    char fname[13] = "vswap.";
    strcat(fname,extension);

    FILE *file = fopen(fname,"rb");
    if(!file)
        CA_CannotOpen(fname);

    ChunksInFile = 0;
    if (!fread(&ChunksInFile, sizeof(word), 1, file))
    {
        fclose(file);
        return;
    }
    PMSpriteStart = 0;
    if (!fread(&PMSpriteStart, sizeof(word), 1, file))
    {
        fclose(file);
        return;
    }
    PMSoundStart = 0;
    if (!fread(&PMSoundStart, sizeof(word), 1, file))
    {
        fclose(file);
        return;
    }

    uint32_t* pageOffsets = (uint32_t *) malloc((ChunksInFile + 1) * sizeof(int32_t));
    CHECKMALLOCRESULT(pageOffsets);
    if (!fread(pageOffsets, sizeof(uint32_t), ChunksInFile, file))
    {
        free(pageOffsets);
        fclose(file);
        return;
    }

    word *pageLengths = (word *) malloc(ChunksInFile * sizeof(word));
    CHECKMALLOCRESULT(pageLengths);
    if (!fread(pageLengths, sizeof(word), ChunksInFile, file))
    {
        free(pageLengths);
        free(pageOffsets);
        fclose(file);
        return;
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    size_t pageDataSize = fileSize - pageOffsets[0];
    if(pageDataSize > (size_t) -1)
        Quit("The page file \"%s\" is too large!", fname);

    pageOffsets[ChunksInFile] = fileSize;

    uint32_t dataStart = pageOffsets[0];
    int i;

    // Check that all pageOffsets are valid
    for(i = 0; i < ChunksInFile; i++)
    {
        if(!pageOffsets[i]) continue;   // sparse page
        if(pageOffsets[i] < dataStart || pageOffsets[i] >= (size_t) fileSize)
            Quit("Illegal page offset for page %i: %u (filesize: %u)",
                    i, pageOffsets[i], fileSize);
    }

#ifdef WOLF3D_CYD_PORT
    strncpy(PMPageFileName, fname, sizeof(PMPageFileName) - 1);
    PMPageFileName[sizeof(PMPageFileName) - 1] = 0;

    PMPageOffsets = pageOffsets;
    PMPageSizes = (uint32_t *) malloc(ChunksInFile * sizeof(uint32_t));
    CHECKMALLOCRESULT(PMPageSizes);

    for(i = 0; i < ChunksInFile; i++)
    {
        if(!pageOffsets[i])
        {
            PMPageSizes[i] = 0;
            continue;
        }

        if(i + 1 < ChunksInFile && !pageOffsets[i + 1])
            PMPageSizes[i] = pageLengths[i];
        else
            PMPageSizes[i] = pageOffsets[i + 1] - pageOffsets[i];
    }

    PMPages = nullptr;
    PMPageData = nullptr;
    PMPageDataSize = 0;
    PM_PreallocateCache();
    free(pageLengths);
    fclose(file);
    return;
#else
    // Calculate total amount of padding needed for sprites and sound info page
    int alignPadding = 0;
    for(i = PMSpriteStart; i < PMSoundStart; i++)
    {
        if(!pageOffsets[i]) continue;   // sparse page
        uint32_t offs = pageOffsets[i] - dataStart + alignPadding;
        if(offs & 1)
            alignPadding++;
    }

    if((pageOffsets[ChunksInFile - 1] - dataStart + alignPadding) & 1)
        alignPadding++;

    PMPageDataSize = (size_t) pageDataSize + alignPadding;
    PMPageData = (uint32_t *) malloc(PMPageDataSize);
    CHECKMALLOCRESULT(PMPageData);

    PMPages = (uint8_t **) malloc((ChunksInFile + 1) * sizeof(uint8_t *));
    CHECKMALLOCRESULT(PMPages);

    // Load pages and initialize PMPages pointers
    uint8_t *ptr = (uint8_t *) PMPageData;
    for(i = 0; i < ChunksInFile; i++)
    {
        if((i >= PMSpriteStart && i < PMSoundStart) || i == ChunksInFile - 1)
        {
            size_t offs = ptr - (uint8_t *) PMPageData;

            // pad with zeros to make it 2-byte aligned
            if(offs & 1)
            {
                *ptr++ = 0;
                if(i == ChunksInFile - 1) PMSoundInfoPagePadded = true;
            }
        }

        PMPages[i] = ptr;

        if(!pageOffsets[i])
            continue;               // sparse page

        // Use specified page length, when next page is sparse page.
        // Otherwise, calculate size from the offset difference between this and the next page.
        uint32_t size;
        if(!pageOffsets[i + 1]) size = pageLengths[i];
        else size = pageOffsets[i + 1] - pageOffsets[i];

        fseek(file, pageOffsets[i], SEEK_SET);
        if (!fread(ptr, 1, size, file))
        {
            free(pageLengths);
            free(pageOffsets);
            fclose(file);
            return;
        }
        ptr += size;
    }

    // last page points after page buffer
    PMPages[ChunksInFile] = ptr;

    free(pageLengths);
    free(pageOffsets);
    fclose(file);
#endif
}

void PM_Shutdown()
{
#ifdef WOLF3D_CYD_PORT
    PM_ClearCache();
    free(PMPageSizes);
    PMPageSizes = nullptr;
    free(PMPageOffsets);
    PMPageOffsets = nullptr;
#else
    free(PMPages);
    free(PMPageData);
#endif
}

#ifdef WOLF3D_CYD_PORT
uint32_t PM_GetPageSize(int page)
{
    if(page < 0 || page >= ChunksInFile)
        Quit("PM_GetPageSize: Tried to access illegal page: %i", page);
    return PMPageSizes ? PMPageSizes[page] : 0;
}

static uint8_t *PM_GetPageInternal(int page, bool allowHotPageCache)
{
    if(page < 0 || page >= ChunksInFile)
        Quit("PM_GetPage: Tried to access illegal page: %i", page);

    uint32_t size = PM_GetPageSize(page);
    if(size == 0) return PMEmptyPage;

    if(allowHotPageCache)
    {
        uint8_t *hotPage = PM_FindHotPage(page);
        if(hotPage)
        {
            PMStatHits++;
            cyd_trace_pm_hit(page);
            PM_LogStats();
            return hotPage;
        }

        hotPage = PM_LoadHotPage(page, size);
        if(hotPage)
        {
            PM_LogStats();
            return hotPage;
        }
    }

    for(int i = 0; i < PM_CACHE_SLOTS; ++i)
    {
        if(PMCache[i].page == page)
        {
            PMCache[i].lastUse = ++PMCacheClock;
            PMStatHits++;
            cyd_trace_pm_hit(page);
            PM_LogStats();
            return PMCache[i].data;
        }
    }

    PMStatMisses++;
    int slotIndex = PM_ChooseSlot();
    if(slotIndex < 0)
    {
        furi_log_print_format(2, "Wolf3D",
            "PM no unpinned cache slot for page %i size %u heap %u max %u",
            page, (unsigned)size, esp_get_free_heap_size(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        return nullptr;
    }
    PMCacheSlot &slot = PMCache[slotIndex];
    if(slot.capacity < size)
    {
        const uint32_t freeHeap = esp_get_free_heap_size();
        const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        if(freeHeap < size + PM_MIN_FREE_AFTER_ALLOC || largest < size)
        {
            furi_log_print_format(2, "Wolf3D",
                "PM reuse slot %i for page %i size %u heap %u max %u",
                slotIndex, page, size, freeHeap, largest);
        }

        free(slot.data);
        slot.data = nullptr;
        slot.capacity = 0;
        slot.page = -1;
        slot.pinned = false;

        uint32_t allocSize = size;
        if(freeHeap >= PM_PREALLOC_SIZE + PM_MIN_FREE_AFTER_ALLOC &&
           largest >= PM_PREALLOC_SIZE &&
           PM_PREALLOC_SIZE > size)
        {
            allocSize = PM_PREALLOC_SIZE;
        }

        uint8_t *newData = (uint8_t *) malloc(allocSize);
        CHECKMALLOCRESULT(newData);
        slot.data = newData;
        slot.capacity = allocSize;
    }

    uint32_t readUs = 0;
    if(!PM_ReadPageData(page, slot.data, size, &readUs))
        Quit("Could not read VSWAP page %i", page);
    PMStatReadUs += readUs;
    cyd_trace_pm_miss(page, size, readUs);

    slot.page = page;
    slot.lastUse = ++PMCacheClock;
    slot.pinned = false;
    PM_LogStats();
    return slot.data;
}

uint8_t *PM_GetPage(int page)
{
    return PM_GetPageInternal(page, true);
}

uint8_t *PM_PinPage(int page)
{
    uint8_t *data = PM_GetPageInternal(page, false);
    for(int i = 0; i < PM_CACHE_SLOTS; ++i)
    {
        if(PMCache[i].page == page)
        {
            PMCache[i].pinned = true;
            PMCache[i].lastUse = ++PMCacheClock;
            break;
        }
    }
    return data;
}

void PM_UnpinPage(int page)
{
    for(int i = 0; i < PM_CACHE_SLOTS; ++i)
    {
        if(PMCache[i].page == page)
        {
            PMCache[i].pinned = false;
            PMCache[i].lastUse = ++PMCacheClock;
            break;
        }
    }
}

uint8_t *PM_GetEnd()
{
    return nullptr;
}
#endif
