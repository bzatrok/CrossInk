# Render pipeline map — Xteink X4 Pro (env x4-pro)

Read-only investigation. freeink-sdk pinned at 918136b. Paths relative to /Users/bzatrok/dev/CrossInk.
Abbreviations: FID = freeink-sdk/libs/display/FreeInkDisplay. Display-driver files are under FID/src/driver/.

## 1. Pixel buffer ownership

- **Owner is the SDK display facade.** `FreeInkDisplay::begin()` allocates once at `FID/src/FreeInkDisplay.cpp:221`.
- **Allocation is PSRAM-first.** `allocFrameBufferStorage()` calls `heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM)` under `FREEINK_FB_PSRAM`, then falls back to `malloc` (`FID/src/FreeInkDisplay.cpp:385-389`).
- **Size comes from driver geometry** (`FID/src/FreeInkDisplay.cpp:210-214`): 800/8 x 480 = 48000 bytes.
- **Single-buffer mode.** Only `frameBuffer0` exists; `frameBuffer1` and `frameBufferActive` are compiled out (`FID/src/FreeInkDisplay.cpp:222-231`; `FID/include/FreeInkDisplay.h:494-499`).
- **Accessor chain:**
  - `FreeInkDisplay::getFrameBuffer()` (`FID/include/FreeInkDisplay.h:316`)
  - `HalDisplay::getFrameBuffer()` (`lib/hal/HalDisplay.cpp:140`)
  - `GfxRenderer::begin()` caches the pointer into `frameBuffer` (`lib/GfxRenderer/GfxRenderer.cpp:239-250`)
  - `GfxRenderer::getFrameBuffer()` (`lib/GfxRenderer/GfxRenderer.cpp:3089`)
- **The drawn buffer is the transmitted buffer.** There is no back buffer and no conversion:
  - `FreeInkDisplay::displayBuffer` (single-buffer branch) calls `_driver->display(_bus, frameBuffer, nullptr, ...)` (`FID/src/FreeInkDisplay.cpp:600-603`).
  - `Ssd1677Driver::displayImpl` calls `writeRam(bus, CMD_WRITE_RAM_BW, fb, _bufferSize)` (`FID/src/driver/Ssd1677Driver.cpp:524-527`).
  - `Ssd1677Driver::writeRam` calls `bus.data(data, size)` (`Ssd1677Driver.cpp:281-284`).
  - `EpdBus::data` calls `SPI.writeBytes(d, len)` (`FID/src/bus/EpdBus.cpp:175-182`).
- **Driver selection on X4 Pro.**
  - Linked drivers: SSD1677 (`freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h:120-122`), plus UC8179 and UC8279_X4 (`BoardConfig.h:149-151`).
  - Runtime pick by boot probe in `selectDriver()` (`FID/src/FreeInkDisplay.cpp:146-170`).
  - Board profile: `DisplayController::SSD1677` (`BoardConfig.h:1634`). SPI is 10 MHz (`BoardConfig.h:882`).
  - SSD1677 config used: `ssd1677DefaultConfig()` (`Ssd1677Driver.cpp:44-72`), unless `FREEINK_X4PRO_FAST_DU_SHORTCUT` is set (`Ssd1677Driver.cpp:166-169`).
- **`GfxRenderer::drawPixel`** is at `lib/GfxRenderer/GfxRenderer.cpp:926-966`:
  - text-clip check (`:927-929`)
  - `rotateCoordinates` (`:935`)
  - bounds check (`:940-942`)
  - strip redirect (`:946-954`)
  - read-modify-write: `target[byteIndex] &= ~(1 << bitPosition)` for black, `|= 1 << bitPosition` for white (`:961-965`)
- **Glyph path does not use `drawPixel`.**
  - `GfxRenderer::drawGlyphBitmap` (`GfxRenderer.cpp:870-900`) calls `glyphBitmap::draw` (`lib/GfxRenderer/GlyphBitmap.h:94-153`).
  - `draw` writes bits with `paint()` (`GlyphBitmap.h:73-81`).
  - It resolves clip, rotation and addressing once per glyph (comment at `GlyphBitmap.h:6-8`; `GfxRenderer.cpp:866-869`).
- **`isPixelBlack`** reads the same buffer (`GfxRenderer.cpp:903-924`).
- **FreeInkUI draws through `GfxRenderer`.**
  - The app uses `freeink::ui::GfxRendererTarget`, for example `src/activities/library/LibraryActivity.h:46` and `src/main.cpp:590`.
  - `GfxRendererTarget` (`freeink-sdk/libs/ui/FreeInkUI/include/FreeInkUIGfxRenderer.h:33`) forwards to `renderer.fillRect`, `renderer.fillRectDither` (`:141-165`) and `renderer.drawPixel` (`:342`).
  - The native `DisplayTarget` (`freeink-sdk/libs/ui/FreeInkUI/include/FreeInkUIDisplayTarget.h:58`) is not used by `src/`. Only `GfxRendererTarget` instances were found.

## 2. Grayscale anti-aliasing

The reader logic lives in `EpubReaderActivity::renderContents` (`src/activities/reader/EpubReaderActivity.cpp:7244-7511`).

- **Pass A: scan only, no pixels.**
  - `PrewarmScope`, then `page->renderText`, then `renderStatusBar()` (`EpubReaderActivity.cpp:7268-7274`).
  - While scanning, `drawText` records codepoints and returns early (`GfxRenderer.cpp:1128-1136`).
- **Pass B: black-and-white compose into the framebuffer.**
  - `composePageBuffer` calls `page->render` (`EpubReaderActivity.cpp:7340-7348`), then `renderStatusBar()` (`:7374-7375`).
  - `Page::render` is `renderText` followed by `renderImages` (`lib/Epub/Epub/Page.cpp:475-479`).
- **Grayscale decision.**
  - `needsTextGrayscale = SETTINGS.textAntiAliasing && foregroundBlack && !fontUsesMonochromeRaster` (`EpubReaderActivity.cpp:7302-7303`).
  - `tiledGrayscale = needsAnyGrayscale && renderer.supportsStripGrayscale()` (`:7410`).
  - `overlapRefresh = tiledGrayscale && !pageHasImages && pagesUntilFullRefresh > 1 && supportsAsyncGrayscaleBase()` (`:7411-7412`).
- **Base refresh, text-only page** (`EpubReaderActivity.cpp:7450-7466`):
  - **Cleanup turn:** `displayBuffer(HALF)` + `preconditionGrayscale()` (`:7451-7457`).
  - **Overlap turn:** `ReaderUtils::displayWithRefreshCycle(..., async=true)` (`:7458-7459`). That calls `renderer.displayBufferAsync` (`src/activities/reader/ReaderUtils.h:245-246`), then `HalDisplay::displayBufferAsync`, then `einkDisplay.displayBufferAsyncNoShadow` (`lib/hal/HalDisplay.cpp:99-105`).
  - **Otherwise:** `displayGrayscaleBase(FAST)` (`:7464`). On SSD1677 this is `beginGrayscale`, then the `PanelDriver` default `displayGrayscaleBase`, which is a plain `display(bus, fb, nullptr, fallback, turnOff)` (`FID/src/driver/PanelDriver.h:142-150`; `Ssd1677Driver.cpp:626-630`).
- **SSD1677 overlay capabilities.** `stripUploads = true`, `asyncBase = true` (`FID/src/driver/Ssd1677Driver.h:104-109`). `overlayGrayscale` defaults to true (`Ssd1677Driver.h:59`). The capabilities struct is at `FID/include/GrayscaleCapabilities.h:15-28`.
- **Tiled path** is `EpubGrayscale::runTiledGrayscalePass` (`src/activities/reader/EpubGrayscale.cpp:14-128`).
- **Whole-plane variant** (`EpubGrayscale.cpp:58-84`). It runs only when `asyncRefreshPending` is true and the planes fit.
  - Allocates two 48000-byte PSRAM planes per page turn as function-scope `HeapByteBuffer`s, freed on return (`:43-59`). The fit check needs PSRAM free >= plane + 128 KB (`:47-50`).
  - `renderPlaneToBuffer` does one full page traversal per plane: `beginStripTarget(buffer, 0, displayHeight)`, `clearScreen(0x00)`, `page.render` (`:26-38`; calls at `:66-69`).
  - `renderer.waitRefreshComplete()` (`:71`).
  - `writeGrayscalePlaneStrip` with all rows, once per plane (`:72-77`).
  - `displayGrayBuffer()` (`:81`), then `cleanupGrayscaleWithFrameBuffer()` (`:82`).
- **Band variant** (`EpubGrayscale.cpp:104-127`). It is used when the whole-plane variant cannot run.
  - `GRAYSCALE_STRIP_ROWS = 80` (`src/activities/reader/EpubGrayscale.h:10`), so 480/80 = 6 bands per plane.
  - Per band: `beginStripTarget`, `clearScreen(0x00)`, `page.render`, `endStripTarget`, `writeGrayscalePlaneStrip` (`:106-117`).
  - LSB then MSB gives 12 full page traversals (`:120-122`). Then `displayGrayBuffer` and `cleanupGrayscaleWithFrameBuffer` (`:125-126`).
  - Glyph band culling runs inside `renderCharImpl`, after `getGlyphData` and before bitmap decode (`GfxRenderer.cpp:836-849`).
  - Strip scratch allocation is in `ensureGrayscaleStripScratch`, PSRAM-first (`EpubReaderActivity.cpp:7152-7184`).
- **Legacy whole-framebuffer path.** It applies only if strip support is false (`EpubReaderActivity.cpp:7480-7509`): `storeBwBuffer`, then `clearScreen(0x00)` + `GRAYSCALE_LSB` render + `copyGrayscaleLsbBuffers`, then the same for MSB, then `displayGrayBuffer`, then `restoreBwBuffer`. Same pattern in `ReaderUtils::renderAntiAliased` (`ReaderUtils.h:261-282`).
- **Panel updates per anti-aliased text page.** There are two waveform refreshes:
  - the black-and-white base refresh;
  - the gray refresh: `Ssd1677Driver::displayGray`, which loads the custom LUT and calls `refresh(bus, Fast, turnOff)` (`Ssd1677Driver.cpp:671-714`, refresh at `:709`).
  - The cleanup turn adds `preconditionGrayscale`, which is a no-op on X4 (comment at `lib/hal/HalDisplay.h:86-90`).
- **Plane encoding per glyph.** `levels` for BW = values 1-3, GrayMSB = 1-2, GrayLSB = 2 only. Gray planes only set bits (`GlyphBitmap.h:97-102`).
- **Images.** `Page.cpp` has no render-mode check. The mode check is in `lib/Epub/Epub/converters/DirectPixelWriter.h:40` and `:110-118`. Image pages use the blank/base sequence at `EpubReaderActivity.cpp:7413-7449`.
- **Display-side plane writes.**
  - `FreeInkDisplay::writeGrayscalePlaneStrip` (`FID/src/FreeInkDisplay.cpp:944-953`) calls `Ssd1677Driver::writeGrayscalePlaneStrip` (`Ssd1677Driver.cpp:662-669`).
  - In Overlay mode, `writeGrayRam` is a plain `writeRam` (`Ssd1677Driver.cpp:632-636`).
- **Cleanup.** `FreeInkDisplay::cleanupGrayscaleBuffers` (`FID/src/FreeInkDisplay.cpp:973-982`) calls `Ssd1677Driver::cleanupGrayscaleBuffers`, which writes 48000 bytes to RED RAM (`Ssd1677Driver.cpp:716-725`).

## 3. Fonts with CROSSINK_SCALABLE_FONTS=1

- **Build flags** (`platformio.ini:424-430`): `-DCROSSINK_SCALABLE_FONTS=1`, `FREEINK_FONT_ENABLE_AUTOHINT`, `NATIVE_HINTING`, `MONOCHROME`. Library: `FreeInkFont=symlink://freeink-sdk/libs/font/FreeInkFont` (`platformio.ini:413-415`).
- **Built-in reader fonts are TrueType outlines in flash.**
  - Bitter and Lexend Deca come from `lib/ScalableFont/ScalableAssets.generated.h`, listed at `lib/ScalableFont/ScalableBuiltins.cpp:15-21`.
  - Opened with `openMemory` (`ScalableBuiltins.cpp:46`). Registered per point size with `renderer.insertFont` (`ScalableBuiltins.cpp:67-73`).
  - Bitmap built-ins are excluded under scalable fonts (`lib/EpdFont/builtinFonts/all.h:5`).
- **TrueType fonts on the SD card** load with `HalScalableFont::openFile` (`lib/EpdFont/SdCardFontManager.cpp:270-272`). They are registered with `insertFont` (`SdCardFontManager.cpp:192`), not `registerSdCardFont`, so `isSdCardFont()` is false for them.
- **Glyph metrics path.**
  - `EpdFont::findGlyph` calls `data->dynamicGlyphHandler` (`lib/EpdFont/EpdFont.cpp:225-226`).
  - That reaches `HalScalableFont::glyph` (`lib/ScalableFont/HalScalableFont.cpp:786-820`) and `Runtime::glyphMetrics` (`HalScalableFont.cpp:184-213`).
  - On a miss it calls `FtFont::metrics26_6`, which does `FT_Load_Glyph` and a `FT_Outline_Get_CBox` or `FT_Render_Glyph` (`freeink-sdk/libs/font/FreeInkFont/src/FtFont.cpp:642-683`).
  - Returned `EpdGlyph`s live in a 32-entry ring per size (`lib/ScalableFont/HalScalableFont.h:76-77`; `HalScalableFont.cpp:791`).
- **Bitmap path.**
  - `GfxRenderer::getGlyphBitmap` calls `fontData->bitmapHandler` (`GfxRenderer.cpp:113-116`).
  - That reaches `HalScalableFont::bitmap` (`HalScalableFont.cpp:821-847`) and `Runtime::packedBitmap` (`HalScalableFont.cpp:228-262`).
  - On a miss it calls `FtFont::rasterize26_6` / `rasterizeGlyph26_6`: `loadGlyph` + `FT_Render_Glyph` (`FtFont.cpp:893-932`).
  - The 8-bit coverage is quantised to 2 bits per pixel with cutoffs 4/8/12 (`HalScalableFont.cpp:248-258`).
- **FreeType runs on the device.** Its source is compiled in (`freeink-sdk/libs/font/FreeInkFont/src/freetype/*.c`). `ensureSize26_6` caches only the last size per face and calls `FT_Set_Char_Size` on change (`FtFont.cpp:576-586`). `FT_Outline_Embolden` applies for synthetic bold (`FtFont.cpp:622-632`).
- **Glyph cache location and memory.**
  - A single process-wide `Runtime` comes from `runtime()` (`HalScalableFont.cpp:307-310`).
  - The pool is `MemoryPool::Psram` on hardware (`HalScalableFont.cpp:16-20`).
  - FreeType workspace: 1280, 1024 or 768 KB, whichever allocates (`HalScalableFont.cpp:23`, `:271-280`).
  - Pixel arena: `PixelBytes` 512 KB plus tables (`:24`, `:133-137`, `:299-302`).
  - Slots: pixel, metric and kerning tables have 512 each (`:25-27`).
  - Per-face size descriptors are allocated in PSRAM (`HalScalableFont.cpp:375`).
  - The streamed-font prefix cache holds up to 1 MB in PSRAM (`HalScalableFont.cpp:587-597`).
- **Cache policy.**
  - Key: `(cacheId << 40) | (points << 32) | glyph` (`HalScalableFont.cpp:157-159`).
  - Index: `(glyph * 31u + points * 7u + cacheId) % slots` (`HalScalableFont.cpp:161-164`).
  - Direct-mapped, one entry per slot; a collision overwrites (`HalScalableFont.cpp:231-232`, `:259`).
  - Pixel storage is a bump allocator. On overflow, `clearPixels()` drops all bitmaps (`HalScalableFont.cpp:244`, `:166-170`).
  - `setRenderOptions` clears all caches (`HalScalableFont.cpp:440`).
- **When rasterisation happens.** The cache persists across page turns. FreeType runs only on a miss: a new glyph or size, a slot collision, or an arena wipe.
- **Per-turn prewarm.**
  - The scan pass records unique codepoints (`lib/GfxRenderer/FontCacheManager.cpp:147-196`). The cap is 512 (`lib/GfxRenderer/FontCacheManager.h:62`). Deduplication is a linear search (`FontCacheManager.cpp:176-181`).
  - `endScanAndPrewarm` (`FontCacheManager.cpp:211-250`) calls `prewarmCache`.
  - The scalable branch walks the text and calls `getGlyphData` and `bitmapHandler` per codepoint (`FontCacheManager.cpp:81-104`).
  - `PrewarmScope` construction calls `clearCache()`, which clears only the decompressor and SD-font caches, not the TrueType runtime (`FontCacheManager.cpp:43-48`, `:200-209`).
- **Lookups per glyph inside `GfxRenderer::drawText`** (`GfxRenderer.cpp:1113-1269`):
  - `applyLigatures` (`:1167`)
  - `getFallbackCodepoint`, which does one `findGlyphData` (`:1169`; `lib/EpdFont/EpdFontFamily.cpp:233-234`)
  - `findGlyphData` (`:1170`)
  - `getKerning`, which calls `HalScalableFont::kerning` with two `FT_Get_Char_Index` plus a kerning cache (`:1176`; `HalScalableFont.cpp:854-861`)
  - `getGlyph` (`:1232`)
  - `renderCharImpl`, which calls `getGlyphData` (`:822`) and then `getGlyphBitmap` (`:851`)
  - That adds up to 4 metric-cache lookups per glyph. Each handler opens a `ScalableFontAccess` scope (`HalScalableFont.cpp:335-342`).
- **Per word.** `TextBlock::render` calls `drawText` per word (`lib/Epub/Epub/blocks/TextBlock.cpp:239-289`). `drawText` does a `fontMap.find` per call (`GfxRenderer.cpp:1141`) and `getFontAscenderSize`, which is another `fontMap.find` plus a `ScalableFontAccess` (`GfxRenderer.cpp:1121`, `:2927-2938`).
- **Idle next-page prewarm skips TrueType fonts.** It is gated by `renderer.isSdCardFont(renderFontId)` (`EpubReaderActivity.cpp:2745`).

## 4. EPUB page-turn work before drawing

1. **Turn request.** `requestManualPageTurn` (`EpubReaderActivity.cpp:5825-5855`) enforces `MIN_MANUAL_PAGE_TURN_GAP_MS = 200` (`:110`, `:5849`). `pageTurn` changes `section->currentPage` and calls `requestUpdate()` (`:5909-5986`).
2. **Render task wake-up.**
   - `Activity::requestUpdate` (`src/activities/Activity.cpp:13`) leads to `ActivityManager::requestUpdate`, which sets a deferred flag (`src/activities/ActivityManager.cpp:1324-1334`).
   - The flag is consumed at the end of the loop with `xTaskNotify` (`ActivityManager.cpp:726-730`).
   - `renderTaskLoop` (`ActivityManager.cpp:476-503`) takes `RenderLock`, `HalPowerManager::Lock`, `display.setInverted(...)`, then calls `currentActivity->render`.
   - The render task is pinned to core 1 at priority 1 (`ActivityManager.cpp:456-466`). Its stack is 24576 bytes on S3 (`src/main.cpp:376`).
3. **Within-section turn: no layout.** Catch-up runs only if `currentPage >= pageCount` on a partial or building section (`EpubReaderActivity.cpp:6587-6637`). The chapter-group estimate is cached (`EpubReaderActivity.cpp:7864-7867`).
4. **Clear.** `renderer.clearScreen(ReaderUtils::readerBackgroundColor())` (`EpubReaderActivity.cpp:6673`).
5. **SD read: the page.**
   - `section->loadPage(section->currentPage)` (`EpubReaderActivity.cpp:6700`) calls `Section::loadPage` (`lib/Epub/Epub/Section.cpp:1343-1357`).
   - `Section::loadPageAt` (`Section.cpp:1321-1341`): `Storage.openFileForRead`, seek to header, read `lutOffset`, seek into the lookup table, read `pagePos`, seek, then `Page::deserialize`.
   - During a live build it uses `loadPageDuringBuild` instead (`Section.cpp:1292-1316`).
   - `Page::deserialize` (`lib/Epub/Epub/Page.cpp:612-713`) does per-element tags, then `PageLine::deserialize` (`Page.cpp:55-75`), then `TextBlock::deserialize` (`TextBlock.cpp:429-540`).
   - `TextBlock::deserialize` does one arena read (`TextBlock.cpp:477-485`) plus 16 separate `tryReadPod` calls for block style (`TextBlock.cpp:519-534`).
6. **SD reads: status bar.** `renderStatusBar` runs twice per turn, once in the scan and once in the compose (`EpubReaderActivity.cpp:7273`, `:7375`).
   - Chapter title: `epub->getTocIndexForSpineIndex` and `getTocItem` (`EpubReaderActivity.cpp:7780-7784`).
   - These call `BookMetadataCache::getSpineEntry` and `getTocEntry`, which seek and read the open book file (`lib/Epub/Epub/BookMetadataCache.cpp:604-609`, `:653-658`).
7. **SD reads: streamed TrueType on a glyph miss only.** `HalScalableFont::streamRead` serves the PSRAM prefix (`HalScalableFont.cpp:665-669`) or one of four 1 KB windows (`:671-678`). Otherwise it does `seekSet` + `read` on the SD card (`:682-700`).
8. **SD reads: image pages only.** `page->prepareImageCaches()` runs under `FrameBufferLoan` (`EpubReaderActivity.cpp:7365-7373`; `Page.cpp:715-721`).
9. **Post-draw work inside `render()`:**
   - debounced progress save (`EpubReaderActivity.cpp:6762-6769`; debouncer at `:7146`);
   - `silentIndexNextChapterIfNeeded` (`:6770`);
   - `queueCompletionPromptIfNeeded` (`:6771`).
10. **Not on the within-section hot path:** ZIP inflate and HTML layout. They happen only on a section build or a cache miss (`EpubReaderActivity.cpp:6104-6250`).

## 5. Dirty rectangles, diffing, partial windows

- **No app-level partial update.** `GfxRenderer::displayWindow` is commented out (`lib/GfxRenderer/GfxRenderer.h:228-229`). `HalDisplay` exposes no window API (`lib/hal/HalDisplay.h`).
- **SDK window path exists but is unused.**
  - `FreeInkDisplay::displayWindow` (`FID/src/FreeInkDisplay.cpp:806-825`) calls `Ssd1677Driver::displayWindow` (`Ssd1677Driver.cpp:554-599`).
  - That driver function heap-allocates `std::vector` row copies (`:572`, `:584`).
  - No caller exists in `src/` or `lib/`. A `rg` for `displayWindow` found only the SDK definition and the commented header line.
- **No host-side diff of old and new frames.** The differential compare is done by the controller against RED RAM (comment at `Ssd1677Driver.cpp:526-532`).
- **Every black-and-white update is full-frame.** `setRamArea(bus, 0, 0, _w, _h)` is followed by 48000-byte writes (`Ssd1677Driver.cpp:506`, `:524-527`). Gray uploads are row strips but cover all rows (`Ssd1677Driver.cpp:662-669`).
- **Region helpers touch framebuffer bytes only, never the panel:**
  - `readFramebufferRegion` and `writeFramebufferRegion` (`GfxRenderer.cpp:2399-2430`);
  - `copyRegionToBuffer` and `copyBufferToRegion` (`GfxRenderer.cpp:2664`, `:2682`);
  - used for toasts, highlights and covers (`EpubReaderActivity.cpp:5719`, `src/util/WordSelectNavigator.cpp:476`, `:488`, `src/activities/home/HomeActivity.cpp:1099`).
- **Text-clip rect is draw-time clipping only** (`GfxRenderer.cpp:1099-1111`, `:875-878`).
- **"dirty" hits in the code are unrelated store flags,** for example `src/BookmarkStore.cpp:308` and `src/SdCardFontSystem.cpp:157`.

## 6. Orientation and full-buffer passes

- **Rotation happens at draw time.** `rotateCoordinates` (`GfxRenderer.cpp:378-407`). Portrait maps `phyX = y` and `phyY = panelHeight - 1 - x` (`:381-386`).
  - The reader sets orientation through `applyOrientation` and `renderer.setOrientation` (`EpubReaderActivity.cpp:5750-5778`).
  - Glyphs rotate their origin and both axes once per glyph (`GfxRenderer.cpp:885-895`).
  - The walk uses `stepX = dxY*strideBits + dxX` (`GlyphBitmap.h:118`). In Portrait, consecutive text-row pixels sit one physical row apart, which is 100 bytes in the PSRAM buffer.
  - `fillRectImpl` rotates only the corners and writes whole bytes (`GfxRenderer.h:125-129`; `GfxRenderer.cpp:1499`).
- **No whole-buffer rotate before transfer.**
  - SSD1677 mirroring is done by the controller's data-entry mode in `setRamArea` (`Ssd1677Driver.cpp:243-279`).
  - The host-side `sendPlaneFlipped` is used only by the X3 driver (`FID/src/driver/Uc8253X3Driver.cpp:217`).
- **Full-buffer passes that exist:**
  - **Night-mode inversion.** `invertBytes` (`FID/src/FreeInkDisplay.cpp:73-78`) runs twice over 48000 bytes around every blocking display, only when `_inverted` (`:601-603`). Async paths fall back to blocking when inverted (`:635-637`).
  - **Shadowed async copy.** `memcpy(_asyncShadow, frameBuffer, bufferSize)` (`FreeInkDisplay.cpp:679`). The reader's overlap path uses `noShadow` (`:648-657`), so no copy happens there.
  - **`cleanupGrayscaleBuffers` memcpy.** It runs only if `bwBuffer != frameBuffer` (`FreeInkDisplay.cpp:981`). `GfxRenderer::cleanupGrayscaleWithFrameBuffer` passes `frameBuffer` itself (`GfxRenderer.cpp:3226-3230`), so no copy happens.
  - **Absolute grayscale complement loop.** Absolute mode only (`Ssd1677Driver.cpp:637-647`). The reader uses Overlay.
  - **Post-refresh resync on blocking updates.** BW + RED rewrite of the full frame when `prev == nullptr && !async` (`Ssd1677Driver.cpp:543-547`). A blocking FAST update therefore sends 3 x 48000 bytes. An async update sends 1 x 48000 bytes (`:527`), and the RED rewrite comes later in cleanup.
  - **Legacy only.** `storeBwBuffer` and `restoreBwBuffer` copy six 8 KB chunks (`GfxRenderer.cpp:3162-3220`; chunk size at `GfxRenderer.h:50`).
  - **Unused in the reader.** `GfxRenderer::invertScreen` loops over the whole buffer (`GfxRenderer.cpp:2345-2349`).

## 7. Clearing

- **Every reader frame starts with a full memset.** `EpubReaderActivity.cpp:6673` calls `GfxRenderer::clearScreen` (`GfxRenderer.cpp:2303-2310`), then `HalDisplay::clearScreen` (`lib/hal/HalDisplay.cpp:51`), then `memset(frameBuffer, color, bufferSize)` (`FID/src/FreeInkDisplay.cpp:240`).
- **Strip mode clears only the strip scratch** (`GfxRenderer.cpp:2304-2307`). This happens per plane in the whole-plane variant (`EpubGrayscale.cpp:31`) and per band in the band variant (`EpubGrayscale.cpp:109`).
- **Image pages add up to two more full clears** (`EpubReaderActivity.cpp:7363`, `:7372`).
- **Error and empty-chapter paths also clear** (`EpubReaderActivity.cpp:6716`, `:6741`).
- **`returnBuildStorage` memsets 48000 bytes** after each `FrameBufferLoan` (`FID/src/FreeInkDisplay.cpp:447-453`).
- **The legacy path clears to 0x00 before each plane** (`EpubReaderActivity.cpp:7488`, `:7494`).

## 8. Existing timing instrumentation

- **Active at runtime.** `EpdBus::waitBusy` and `EpdBus::waitRefreshComplete` print `"[%lu]   Wait complete: %s (%lu ms)"` via `Serial.printf` when `tag && Serial` (`FID/src/bus/EpdBus.cpp:319-321`, `:401-403`). SSD1677 passes the tag `"refresh"` (`Ssd1677Driver.cpp:391`, `:437`), so every refresh logs its busy-wait duration when USB serial is connected.
- **Compiled out unless `SSD1677_PROBE_DEBUG`:** `[SSD1677] %s refresh %ums` (`Ssd1677Driver.cpp:288-291`, `:351-354`, `:393-396`).
- **Compiled out on x4-pro.** `LOG_LEVEL=1` (`platformio.ini:450`) removes `LOG_DBG` (`lib/Logging/Logging.h:43-49`). That removes the `"Idle SD font prewarm ... in %lums"` line (`EpubReaderActivity.cpp:2783`) and the `storeBwBuffer` debug lines (`GfxRenderer.cpp:3186`, `:3219`).
- **Timestamps recorded but not logged as durations:**
  - `lastRenderCompleteMs = millis()` (`EpubReaderActivity.cpp:6748`)
  - `pageShownAtMs` (`:6756`)
  - `lastPageTurnTime` (`:5984`)
  - build popup deadline, section builds only (`:6263`, `:6278`)
- **Not found:** timers around `loadPage`, `Page::render`, the prewarm, FreeType rasterisation, the grayscale passes, or `clearScreen`. No `esp_timer_get_time` or `micros()` in `lib/GfxRenderer`, `Page.cpp`, `Section.cpp`, `EpubGrayscale.cpp`, `HalScalableFont.cpp`, `HalDisplay.cpp`, `FreeInkDisplay.cpp` or `Ssd1677Driver.cpp`.

## Ordered chain: text-only page, anti-aliasing on, non-cleanup overlap turn, SSD1677

1. `requestManualPageTurn` (`EpubReaderActivity.cpp:5825`), then `pageTurn` (`:5909`), then `requestUpdate` (`:5985`).
2. `ActivityManager::loop` end calls `xTaskNotify` (`ActivityManager.cpp:726-730`). `renderTaskLoop` takes `RenderLock` and calls `display.setInverted` (`:481-488`).
3. `EpubReaderActivity::render` (`EpubReaderActivity.cpp:5989`) calls `clearScreen`, a 48 KB memset (`:6673`).
4. `Section::loadPage`, then `loadPageAt` opens the file, seeks, and calls `Page::deserialize` (`Section.cpp:1343`, `:1321`; `Page.cpp:612`).
5. `renderContents` (`EpubReaderActivity.cpp:7244`) runs the scan: `page->renderText` + `renderStatusBar`, which does book metadata SD reads (`:7270-7273`).
6. `PrewarmScope::endScanAndPrewarm` calls `prewarmCache`, then `HalScalableFont::glyph` and `bitmap`. On a miss it reaches FreeType (`FontCacheManager.cpp:211`, `:81-104`).
7. `composePageBuffer` calls `Page::render`, then `TextBlock::render`, `GfxRenderer::drawText`, `renderCharImpl`, `drawGlyphBitmap` and `glyphBitmap::draw` into the PSRAM framebuffer (`EpubReaderActivity.cpp:7345`).
8. `renderStatusBar` runs a second time (`EpubReaderActivity.cpp:7375`).
9. `displayWithRefreshCycle(async)` calls `displayBufferAsync(FAST)`, then `displayBufferAsyncNoShadow`, then `Ssd1677Driver::displayStart`. That writes 48 KB BW and fires the waveform without waiting (`ReaderUtils.h:246`; `FID/src/FreeInkDisplay.cpp:653`; `Ssd1677Driver.cpp:430-434`).
10. `runTiledGrayscalePass` (`EpubGrayscale.cpp:14`):
    - allocates two 48 KB PSRAM planes;
    - renders the LSB plane (full traversal) and the MSB plane (full traversal);
    - `waitRefreshComplete`;
    - writes 2 x 48 KB as plane strips;
    - `displayGrayBuffer`, which is the gray LUT refresh and blocks;
    - `cleanupGrayscaleBuffers`, which writes 48 KB to RED.

    The band fallback does 6 strips x 2 planes = 12 traversals instead.
11. Back in `render()`: debounced progress save and next-chapter silent indexing (`EpubReaderActivity.cpp:6764`, `:6770`).

**The buffer is complete at step 8.** Steps 9-10 transfer it and produce the grayscale overlay.

## UNVERIFIED

- Whether `SPI.writeBytes` uses DMA, or how fast it is, when the source buffer is in PSRAM.
- Where `malloc` places the 8 KB chunks in `storeBwBuffer` on this S3 build (internal RAM or PSRAM).
- Which of SSD1677, UC8179 or UC8279 a given X4 Pro unit selects at boot.
- SdFat sector-cache behaviour behind the small `tryReadPod` reads.
- Whether the user's active reader font is a built-in TrueType font, a TrueType font on the SD card, or an SD `.cpfont`. This decides which font path applies.
