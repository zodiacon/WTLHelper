#include "MarkdownView.h"

#include <windowsx.h>
#include <shellapi.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "settings.h"
#include "d2d_init.h"
#include "utils.h"
#include "search.h"
#include "render.h"
#include "file_utils.h"
#include "overlays.h"
#include "input.h"
#include "editor.h"

void render(App& app) {
    if (!app.renderTarget) return;

    app.renderTarget->BeginDraw();
    app.drawCalls = 0;

    if (app.layoutDirty) {
        if (app.editMode) {
            // Edit mode needs complete scroll anchors for preview sync
            layoutDocument(app);
        } else {
            // Lay out the visible region first so this frame presents
            // immediately; the rest continues in WM_APP_LAYOUT_CHUNK slices
            layoutDocumentViewportFirst(app);
            if (!app.layoutComplete) {
                PostMessage(app.hwnd, WM_APP_LAYOUT_CHUNK, 0, 0);
            }
        }
    }

    // Sync preview scroll to editor scroll position using source-offset anchors
    if (app.editMode && !app.scrollAnchors.empty() && !app.editorLineByteOffsets.empty()) {
        // Find the editor's top visible line
        float lineHeight = app.editorTextFormat ? app.editorTextFormat->GetFontSize() * 1.5f : 20.0f;
        int topLine = (int)(app.editorScrollY / lineHeight);
        topLine = std::max(0, std::min(topLine, (int)app.editorLineByteOffsets.size() - 1));
        size_t topByteOffset = app.editorLineByteOffsets[topLine];

        // Binary search for the anchor just before this byte offset
        size_t lo = 0, hi = app.scrollAnchors.size();
        while (lo + 1 < hi) {
            size_t mid = (lo + hi) / 2;
            if (app.scrollAnchors[mid].sourceOffset <= topByteOffset) lo = mid;
            else hi = mid;
        }

        // Interpolate between anchor[lo] and anchor[lo+1]
        float targetY;
        if (lo + 1 < app.scrollAnchors.size() &&
            app.scrollAnchors[lo + 1].sourceOffset > app.scrollAnchors[lo].sourceOffset) {
            float t = (float)(topByteOffset - app.scrollAnchors[lo].sourceOffset) /
                      (float)(app.scrollAnchors[lo + 1].sourceOffset - app.scrollAnchors[lo].sourceOffset);
            t = std::max(0.0f, std::min(t, 1.0f));
            targetY = app.scrollAnchors[lo].renderedY +
                       t * (app.scrollAnchors[lo + 1].renderedY - app.scrollAnchors[lo].renderedY);
        } else {
            // Last anchor or single anchor — use ratio for remaining content
            targetY = app.scrollAnchors[lo].renderedY;
            if (app.contentHeight > app.scrollAnchors[lo].renderedY) {
                size_t lastOffset = app.scrollAnchors[lo].sourceOffset;
                size_t totalBytes = app.editorLineByteOffsets.back();
                if (totalBytes > lastOffset) {
                    float t = (float)(topByteOffset - lastOffset) / (float)(totalBytes - lastOffset);
                    t = std::max(0.0f, std::min(t, 1.0f));
                    targetY += t * (app.contentHeight - app.scrollAnchors[lo].renderedY);
                }
            }
        }

        float previewMaxScroll = std::max(0.0f, app.contentHeight - (float)app.height);
        app.scrollY = std::max(0.0f, std::min(targetY, previewMaxScroll));
        app.targetScrollY = app.scrollY;
    }

    // Edit mode: split view rendering
    if (app.editMode) {
        app.renderTarget->Clear(app.theme.background);

        float editorWidth = app.width * app.editorSplitRatio - 3;
        float previewX = app.width * app.editorSplitRatio + 3;
        float previewWidth = app.width - previewX;

        // Render editor (left pane)
        renderEditor(app, editorWidth);

        // Render separator
        renderSeparator(app);

        // Render preview (right pane) using clip + transform
        app.renderTarget->PushAxisAlignedClip(
            D2D1::RectF(previewX, 0, (float)app.width, (float)app.height),
            D2D1_ANTIALIAS_MODE_ALIASED);

        D2D1_MATRIX_3X2_F originalTransform;
        app.renderTarget->GetTransform(&originalTransform);
        app.renderTarget->SetTransform(
            D2D1::Matrix3x2F::Translation(previewX, 0) * originalTransform);

        // Clear preview background
        app.brush->SetColor(app.theme.background);
        app.renderTarget->FillRectangle(
            D2D1::RectF(0, 0, previewWidth, (float)app.height), app.brush);

        goto render_document;
    }

    // Clear background
    app.renderTarget->Clear(app.theme.background);
    app.drawCalls++;

render_document:

    // Clamp scroll values
    float maxScrollX = std::max(0.0f, app.contentWidth - app.width);
    float maxScrollY = std::max(0.0f, app.contentHeight - app.height);
    app.scrollX = std::max(0.0f, std::min(app.scrollX, maxScrollX));
    app.scrollY = std::max(0.0f, std::min(app.scrollY, maxScrollY));

    // Render cached layout (document coordinates -> screen)
    const float viewportTop = app.scrollY;
    const float viewportBottom = app.scrollY + app.height;
    const float viewportLeft = app.scrollX;
    const float viewportRight = app.scrollX + app.width;
    const float cullMargin = 100.0f;

    for (const auto& rect : app.layoutRects) {
        if (rect.rect.bottom < viewportTop - cullMargin ||
            rect.rect.top > viewportBottom + cullMargin) {
            continue;
        }
        if (rect.rect.right < viewportLeft - cullMargin ||
            rect.rect.left > viewportRight + cullMargin) {
            continue;
        }
        app.brush->SetColor(rect.color);
        app.renderTarget->FillRectangle(
            D2D1::RectF(rect.rect.left - app.scrollX, rect.rect.top - app.scrollY,
                       rect.rect.right - app.scrollX, rect.rect.bottom - app.scrollY),
            app.brush);
        app.drawCalls++;
    }

    // Render images (bitmaps)
    for (const auto& bmp : app.layoutBitmaps) {
        if (!bmp.bitmap) continue;
        if (bmp.destRect.bottom < viewportTop - cullMargin ||
            bmp.destRect.top > viewportBottom + cullMargin) continue;
        if (bmp.destRect.right < viewportLeft - cullMargin ||
            bmp.destRect.left > viewportRight + cullMargin) continue;
        app.renderTarget->DrawBitmap(bmp.bitmap,
            D2D1::RectF(bmp.destRect.left - app.scrollX,
                         bmp.destRect.top - app.scrollY,
                         bmp.destRect.right - app.scrollX,
                         bmp.destRect.bottom - app.scrollY));
        app.drawCalls++;
    }

    for (const auto& run : app.layoutTextRuns) {
        if (run.bounds.bottom < viewportTop - cullMargin ||
            run.bounds.top > viewportBottom + cullMargin) {
            continue;
        }
        if (run.bounds.right < viewportLeft - cullMargin ||
            run.bounds.left > viewportRight + cullMargin) {
            continue;
        }
        app.brush->SetColor(run.color);
        D2D1_POINT_2F drawPos = D2D1::Point2F(run.pos.x - app.scrollX, run.pos.y - app.scrollY);
        if (app.deviceContext) {
            app.deviceContext->DrawTextLayout(drawPos, run.layout, app.brush,
                D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        } else {
            app.renderTarget->DrawTextLayout(drawPos, run.layout, app.brush);
        }
        app.drawCalls++;
    }

    for (const auto& line : app.layoutLines) {
        float minY = std::min(line.p1.y, line.p2.y);
        float maxY = std::max(line.p1.y, line.p2.y);
        if (maxY < viewportTop - cullMargin || minY > viewportBottom + cullMargin) {
            continue;
        }
        app.brush->SetColor(line.color);
        app.renderTarget->DrawLine(
            D2D1::Point2F(line.p1.x - app.scrollX, line.p1.y - app.scrollY),
            D2D1::Point2F(line.p2.x - app.scrollX, line.p2.y - app.scrollY),
            app.brush, line.stroke);
        app.drawCalls++;
    }

    // Render code block copy button on hover
    if (app.hoveredCodeBlock >= 0 && app.hoveredCodeBlock < (int)app.codeBlocks.size()) {
        const auto& cb = app.codeBlocks[app.hoveredCodeBlock];
        if (cb.bounds.bottom >= viewportTop - cullMargin &&
            cb.bounds.top <= viewportBottom + cullMargin) {
            float btnW = dpi(app, 52.0f);
            float btnH = dpi(app, 26.0f);
            float btnPad = 8.0f * app.contentScale * app.zoomFactor;
            float btnX = cb.bounds.right - btnW - btnPad - app.scrollX;
            float btnY = cb.bounds.top + btnPad - app.scrollY;

            // Button background
            app.brush->SetColor(D2D1::ColorF(
                app.theme.isDark ? 0.3f : 0.85f,
                app.theme.isDark ? 0.3f : 0.85f,
                app.theme.isDark ? 0.3f : 0.85f,
                0.9f));
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(btnX, btnY, btnX + btnW, btnY + btnH), 4, 4),
                app.brush);

            // "Copy" label centered in button
            app.brush->SetColor(D2D1::ColorF(
                app.theme.isDark ? 0.9f : 0.15f,
                app.theme.isDark ? 0.9f : 0.15f,
                app.theme.isDark ? 0.9f : 0.15f,
                1.0f));
            CComPtr<IDWriteTextLayout> btnLayout;
            app.dwriteFactory->CreateTextLayout(L"Copy", 4, app.codeFormat,
                btnW, btnH, &btnLayout);
            if (btnLayout) {
                btnLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                btnLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                app.renderTarget->DrawTextLayout(
                    D2D1::Point2F(btnX, btnY), btnLayout, app.brush);
            }
            app.drawCalls++;
        }
    }

    // Determine scrollbar visibility
    bool needsVScroll = app.contentHeight > app.height;
    bool needsHScroll = app.contentWidth > app.width;
    float scrollbarSize = dpi(app, 14.0f);

    // Scrollbar color: dark on light themes, light on dark themes
    float sbColorValue = app.theme.isDark ? 1.0f : 0.0f;

    // Draw vertical scrollbar
    if (needsVScroll) {
        float maxScrollY = std::max(0.0f, app.contentHeight - app.height);
        float trackHeight = app.height - (needsHScroll ? scrollbarSize : 0);
        float sbHeight = trackHeight / app.contentHeight * trackHeight;
        sbHeight = std::max(sbHeight, dpi(app, 30.0f));
        float sbY = (maxScrollY > 0) ? (app.scrollY / maxScrollY * (trackHeight - sbHeight)) : 0;

        float sbWidth = (app.scrollbarHovered || app.scrollbarDragging) ? dpi(app, 10.0f) : dpi(app, 6.0f);
        float sbAlpha = (app.scrollbarHovered || app.scrollbarDragging) ? 0.5f : 0.3f;

        app.brush->SetColor(D2D1::ColorF(sbColorValue, sbColorValue, sbColorValue, sbAlpha));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(app.width - sbWidth - dpi(app, 4.0f), sbY,
                                          app.width - dpi(app, 4.0f), sbY + sbHeight), 3, 3),
            app.brush);
        app.drawCalls++;
    }

    // Draw horizontal scrollbar
    if (needsHScroll) {
        float maxScrollX = std::max(0.0f, app.contentWidth - app.width);
        float trackWidth = app.width - (needsVScroll ? scrollbarSize : 0);
        float sbWidth = trackWidth / app.contentWidth * trackWidth;
        sbWidth = std::max(sbWidth, dpi(app, 30.0f));
        float sbX = (maxScrollX > 0) ? (app.scrollX / maxScrollX * (trackWidth - sbWidth)) : 0;

        float sbHeight = (app.hScrollbarHovered || app.hScrollbarDragging) ? dpi(app, 10.0f) : dpi(app, 6.0f);
        float sbAlpha = (app.hScrollbarHovered || app.hScrollbarDragging) ? 0.5f : 0.3f;

        app.brush->SetColor(D2D1::ColorF(sbColorValue, sbColorValue, sbColorValue, sbAlpha));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(sbX, app.height - sbHeight - dpi(app, 4.0f),
                                          sbX + sbWidth, app.height - dpi(app, 4.0f)), 3, 3),
            app.brush);
        app.drawCalls++;
    }

    // Draw selection highlights
    if ((app.selecting || app.hasSelection) && !app.textRects.empty()) {
        // Calculate selection bounds (normalized so start is always before end)
        // Selection is stored in document coordinates
        float selStartX = (float)app.selStartX;
        float selStartY = (float)app.selStartY;
        float selEndX = (float)app.selEndX;
        float selEndY = (float)app.selEndY;

        // Swap if selection was made bottom-to-top
        if (selStartY > selEndY || (selStartY == selEndY && selStartX > selEndX)) {
            std::swap(selStartX, selEndX);
            std::swap(selStartY, selEndY);
        }

        // Check if this is a "select all" (selectedText is set but selection coords are same)
        bool isSelectAll = app.hasSelection && !app.selectedText.empty() &&
                          app.selStartX == app.selEndX && app.selStartY == app.selEndY;

        app.brush->SetColor(D2D1::ColorF(0.2f, 0.4f, 0.9f, 0.35f));

        const auto& lines = app.lineBuckets;

        std::wstring collectedText;
        size_t selectedCount = 0;

        for (size_t i = 0; i < lines.size(); i++) {
            const auto& line = lines[i];
            float lineCenterY = (line.top + line.bottom) / 2;

            bool lineInSelection = false;
            float drawLeft = line.minX;
            float drawRight = line.maxX;

            if (isSelectAll) {
                lineInSelection = true;
            } else if (lineCenterY >= selStartY - 3 && lineCenterY <= selEndY + 3) {
                float lineHeight = line.bottom - line.top;
                bool isSingleLine = (selEndY - selStartY) <= lineHeight;

                if (isSingleLine) {
                    // Single line selection
                    drawLeft = std::max(line.minX, selStartX);
                    drawRight = std::min(line.maxX, selEndX);
                    if (drawLeft < drawRight) lineInSelection = true;
                } else if (lineCenterY < selStartY + lineHeight) {
                    // First line - from selection start to end of line
                    drawLeft = std::max(line.minX, selStartX);
                    lineInSelection = true;
                } else if (lineCenterY > selEndY - lineHeight) {
                    // Last line - from start of line to selection end
                    drawRight = std::min(line.maxX, selEndX);
                    lineInSelection = true;
                } else {
                    // Middle line - full width
                    lineInSelection = true;
                }
            }

            if (lineInSelection) {
                // Draw continuous selection bar for this line
                app.renderTarget->FillRectangle(
                    D2D1::RectF(drawLeft - app.scrollX, line.top - app.scrollY,
                                drawRight - app.scrollX, line.bottom - app.scrollY),
                    app.brush);
                selectedCount++;

                // Collect text from rects in this line that fall within selection
                if (!collectedText.empty()) collectedText += L"\n";
                for (size_t idx : line.textRectIndices) {
                    const auto& tr = app.textRects[idx];
                    const D2D1_RECT_F& rect = tr.rect;
                    if (rect.left < drawRight && rect.right > drawLeft) {
                        if (!collectedText.empty() && collectedText.back() != L'\n') {
                            collectedText += L" ";
                        }
                        std::wstring_view slice = textViewForRect(app, tr);
                        collectedText.append(slice.data(), slice.size());
                    }
                }
            }
        }
        app.drawCalls += selectedCount;

        // Update selectedText for mouse selections (not select-all)
        if (!isSelectAll && app.hasSelection && selectedCount > 0) {
            app.selectedText = collectedText;
        }
    }

    // Draw search match highlights (search live through visible textRects)
    if (app.showSearch && !app.searchQuery.empty() && !app.textRects.empty() && !app.searchMatches.empty()) {
        // Collect visible match rects by intersecting search matches with text rects
        struct VisibleMatch {
            D2D1_RECT_F rect;
            size_t matchIndex;
        };
        std::vector<VisibleMatch> visibleMatches;

        size_t matchIndex = 0;
        for (const auto& tr : app.textRects) {
            size_t textLen = tr.docLength;
            if (textLen == 0) continue;

            size_t rectStart = tr.docStart;
            size_t rectEnd = rectStart + textLen;

            // Advance to first match that could overlap this rect
            while (matchIndex < app.searchMatches.size()) {
                const auto& m = app.searchMatches[matchIndex];
                size_t mEnd = m.startPos + m.length;
                if (mEnd <= rectStart) {
                    matchIndex++;
                    continue;
                }
                break;
            }

            size_t mi = matchIndex;
            while (mi < app.searchMatches.size()) {
                const auto& m = app.searchMatches[mi];
                if (m.startPos >= rectEnd) break;

                size_t mStart = m.startPos;
                size_t mEnd = m.startPos + m.length;
                size_t overlapStart = std::max(rectStart, mStart);
                size_t overlapEnd = std::min(rectEnd, mEnd);

                if (overlapStart < overlapEnd) {
                    float totalWidth = tr.rect.right - tr.rect.left;
                    float charWidth = totalWidth / (float)textLen;
                    float startX = tr.rect.left + (overlapStart - rectStart) * charWidth;
                    float matchWidth = (overlapEnd - overlapStart) * charWidth;

                    // Extend highlight slightly for better visibility
                    D2D1_RECT_F highlightRect = D2D1::RectF(
                        startX - 1, tr.rect.top,
                        startX + matchWidth + 1, tr.rect.bottom
                    );

                    visibleMatches.push_back({highlightRect, mi});
                }

                if (mEnd <= rectEnd) {
                    mi++;
                } else {
                    break;  // Match spans beyond this rect; continue on next rect
                }
            }

            matchIndex = mi;
        }

        // Draw all matches - orange if it's the current match, yellow otherwise
        for (const auto& vm : visibleMatches) {
            bool isCurrent = (app.searchCurrentIndex >= 0 &&
                              vm.matchIndex == (size_t)app.searchCurrentIndex);

            if (isCurrent) {
                app.brush->SetColor(D2D1::ColorF(1.0f, 0.6f, 0.0f, 0.5f));  // Orange
            } else {
                app.brush->SetColor(D2D1::ColorF(1.0f, 0.9f, 0.0f, 0.3f));  // Yellow
            }

            app.renderTarget->FillRectangle(
                D2D1::RectF(vm.rect.left - app.scrollX, vm.rect.top - app.scrollY,
                            vm.rect.right - app.scrollX, vm.rect.bottom - app.scrollY),
                app.brush);
            app.drawCalls++;
        }
    }

    // "Copied!" notification with fade out (cached layout)
    if (app.showCopiedNotification) {
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - app.copiedNotificationStart).count();

        if (elapsed < 2.0f) {
            float alpha = 1.0f;
            if (elapsed > 0.5f) {
                alpha = 1.0f - (elapsed - 0.5f) / 1.5f;
            }
            app.copiedNotificationAlpha = alpha;

            float copyWidth = dpi(app, 100.0f);
            float copyHeight = dpi(app, 26.0f);
            float pillX = (app.width - copyWidth) / 2;
            float pillY = dpi(app, 10.0f);

            app.brush->SetColor(D2D1::ColorF(0.2f, 0.7f, 0.3f, 0.9f * alpha));
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(pillX, pillY, pillX + copyWidth, pillY + copyHeight), 13, 13),
                app.brush);

            // Cache the "Copied!" text layout and metrics across frames
            // (per-instance state, lives on App — see App::clearLayoutCache)
            if (!app.copiedNotificationLayout) {
                app.dwriteFactory->CreateTextLayout(L"Copied!", 7,
                    app.textFormat, copyWidth, copyHeight, &app.copiedNotificationLayout);
                if (app.copiedNotificationLayout) {
                    DWRITE_TEXT_METRICS m;
                    app.copiedNotificationLayout->GetMetrics(&m);
                    app.copiedNotificationTextOffsetX = (copyWidth - m.width) / 2;
                    app.copiedNotificationTextOffsetY = (copyHeight - m.height) / 2;
                }
            }
            if (app.copiedNotificationLayout) {
                app.brush->SetColor(D2D1::ColorF(1, 1, 1, alpha));
                app.renderTarget->DrawTextLayout(
                    D2D1::Point2F(pillX + app.copiedNotificationTextOffsetX, pillY + app.copiedNotificationTextOffsetY),
                    app.copiedNotificationLayout, app.brush);
            }
            app.drawCalls++;
        } else {
            app.showCopiedNotification = false;
        }
    }

    // Draw stats
    if (app.showStats) {
        wchar_t stats[512];
        swprintf(stats, 512,
            L"Parse: %zu us | Layout: %zu us | Draw calls: %zu\n"
            L"Startup: %.1fms (Win: %.1f | D2D: %.1f | DWrite: %.1f | File: %.1f)",
            app.parseTimeUs,
            app.layoutTimeUs,
            app.drawCalls,
            app.metrics.totalStartupUs / 1000.0,
            app.metrics.windowInitUs / 1000.0,
            app.metrics.d2dInitUs / 1000.0,
            app.metrics.dwriteInitUs / 1000.0,
            app.metrics.fileLoadUs / 1000.0);

        float statsWidth = dpi(app, 600.0f);
        float statsHeight = dpi(app, 50.0f);

        app.brush->SetColor(D2D1::ColorF(0, 0, 0, 0.8f));
        app.renderTarget->FillRectangle(
            D2D1::RectF(app.width - statsWidth - dpi(app, 10.0f), app.height - statsHeight - dpi(app, 10.0f),
                       app.width - dpi(app, 10.0f), app.height - dpi(app, 10.0f)),
            app.brush);

        app.brush->SetColor(D2D1::ColorF(0.7f, 0.9f, 0.7f));
        app.renderTarget->DrawText(stats, (UINT32)wcslen(stats), app.codeFormat,
            D2D1::RectF(app.width - statsWidth - dpi(app, 5.0f), app.height - statsHeight - dpi(app, 5.0f),
                       app.width - dpi(app, 15.0f), app.height - dpi(app, 15.0f)),
            app.brush);
    }

    // Render overlays (search overlay handled separately for edit mode)
    if (app.showSearch && !app.editMode) renderSearchOverlay(app);
    if (app.showFolderBrowser) renderFolderBrowser(app);
    if (app.showToc) renderToc(app);
    if (app.showThemeChooser) renderThemeChooser(app);
    if (app.showHelp) renderHelpOverlay(app);

    // Close edit mode split view clipping
    if (app.editMode) {
        D2D1_MATRIX_3X2_F identity = D2D1::Matrix3x2F::Identity();
        app.renderTarget->SetTransform(identity);
        app.renderTarget->PopAxisAlignedClip();

        // Render search overlay in screen coordinates (over editor pane)
        if (app.showSearch) renderSearchOverlay(app);

        // Render edit mode notification (on top of everything)
        renderEditModeNotification(app);
    }

    // "Saved!" notification (reuses "Copied!" infrastructure)

    app.renderTarget->EndDraw();
}

LRESULT CMarkdownView::OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    m_app.hwnd = m_hWnd;

    HICON hIcon = LoadIconW(ModuleHelper::GetResourceInstance(), L"IDI_ICON1");
    if (hIcon) {
        SetIcon(hIcon, TRUE);
        SetIcon(hIcon, FALSE);
    }

    // D2D/DWrite setup — moved here (from WinMain's imperative post-Create
    // sequence in the pre-host-split version) so any host that creates a
    // CMarkdownView gets a fully-initialized view without replicating this
    // sequence itself. Precondition: COM must already be initialized on this
    // thread (see initD2D's comment in d2d_init.cpp).
    m_app.contentScale = GetDpiForWindow(m_hWnd) / 96.0f;

    if (!initD2D(m_app)) {
        ::MessageBoxW(nullptr, L"Failed to initialize Direct2D", L"Error", MB_OK);
        return -1;  // fail window creation
    }

    updateTextFormats(m_app);
    createTypography(m_app);

    RECT rc;
    ::GetClientRect(m_hWnd, &rc);
    m_app.width = rc.right - rc.left;
    m_app.height = rc.bottom - rc.top;

    if (!createRenderTarget(m_app)) {
        ::MessageBoxW(nullptr, L"Failed to create render target", L"Error", MB_OK);
        return -1;  // fail window creation
    }

    bHandled = FALSE;  // let ATL/DefWindowProc finish default creation processing
    return 0;
}

LRESULT CMarkdownView::OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    if (m_app.d2dFactory) {
        m_app.width = LOWORD(lParam);
        m_app.height = HIWORD(lParam);
        createRenderTarget(m_app);
        m_app.layoutDirty = true;
        InvalidateRect(nullptr, FALSE);
    }
    return 0;
}

LRESULT CMarkdownView::OnDpiChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    UINT newDpi = HIWORD(wParam);
    m_app.contentScale = newDpi / 96.0f;

    // Resize window to suggested new size
    RECT* newRect = (RECT*)lParam;
    ::SetWindowPos(m_hWnd, nullptr,
        newRect->left, newRect->top,
        newRect->right - newRect->left,
        newRect->bottom - newRect->top,
        SWP_NOZORDER | SWP_NOACTIVATE);

    // Recreate text formats and render target for new DPI
    updateTextFormats(m_app);
    createRenderTarget(m_app);
    InvalidateRect(nullptr, FALSE);
    return 0;
}

LRESULT CMarkdownView::OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    PAINTSTRUCT ps;
    ::BeginPaint(m_hWnd, &ps);
    render(m_app);
    ::EndPaint(m_hWnd, &ps);
    return 0;
}

LRESULT CMarkdownView::OnMouseWheel(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    handleMouseWheel(m_app, m_hWnd, wParam, lParam);
    return 0;
}

LRESULT CMarkdownView::OnMouseHWheel(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    handleMouseHWheel(m_app, m_hWnd, wParam, lParam);
    return 0;
}

LRESULT CMarkdownView::OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    handleMouseMove(m_app, m_hWnd, lParam);
    return 0;
}

LRESULT CMarkdownView::OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    handleMouseDown(m_app, m_hWnd, wParam, lParam);
    return 0;
}

LRESULT CMarkdownView::OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    handleMouseUp(m_app, m_hWnd, wParam, lParam);
    return 0;
}

LRESULT CMarkdownView::OnSetCursor(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    if (LOWORD(lParam) == HTCLIENT) {
        // We handle cursor in WM_MOUSEMOVE
        return TRUE;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CMarkdownView::OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    handleKeyDown(m_app, m_hWnd, wParam);
    return 0;
}

LRESULT CMarkdownView::OnChar(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    handleCharInput(m_app, m_hWnd, wParam);
    return 0;
}

LRESULT CMarkdownView::OnDropFiles(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    handleDropFiles(m_app, m_hWnd, wParam);
    return 0;
}

LRESULT CMarkdownView::OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    if (wParam == TIMER_FILE_WATCH) handleFileWatchTimer(m_app, m_hWnd);
    if (wParam == TIMER_EDITOR_REPARSE) editorReparse(m_app);
    if (wParam == TIMER_CURSOR_BLINK) {
        m_app.cursorBlinkOn = !m_app.cursorBlinkOn;
        InvalidateRect(nullptr, FALSE);
    }
    if (wParam == TIMER_NOTIFICATION) {
        // Only fading notifications need repaints; the persistent
        // exit-confirm prompt is static until answered
        bool fading = m_app.showCopiedNotification ||
            (m_app.showEditModeNotification && !m_app.confirmExitPending);
        if (fading) {
            InvalidateRect(nullptr, FALSE);
        } else {
            KillTimer(TIMER_NOTIFICATION);
        }
    }
    if (wParam == TIMER_ZOOM_APPLY) {
        if (m_app.zoomFactor != m_app.appliedZoomFactor) {
            // More zoom ticks arrived since the last apply
            updateTextFormats(m_app);
            InvalidateRect(nullptr, FALSE);
            if (m_app.host) m_app.host->OnZoomChanged(m_app.zoomFactor);
        } else {
            KillTimer(TIMER_ZOOM_APPLY);
            m_app.zoomApplyPending = false;
        }
    }
    return 0;
}

LRESULT CMarkdownView::OnLayoutChunk(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    // Continue an incomplete document layout in ~10ms slices, yielding
    // to input between slices
    if (!m_app.layoutDirty && !m_app.layoutComplete) {
        bool done = layoutDocumentContinue(m_app, 10000);
        InvalidateRect(nullptr, FALSE);  // scrollbar grows as layout fills in
        if (!done) 
            PostMessage(WM_APP_LAYOUT_CHUNK);
    }
    return 0;
}

LRESULT CMarkdownView::OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    // Settings persistence and PostQuitMessage are host concerns (a control
    // instance shouldn't decide when the process quits, and settings/window
    // placement are only meaningful in aggregate at the host's top-level
    // window) — see CMainFrame::OnDestroy in main_d2d.cpp.
    KillTimer(TIMER_FILE_WATCH);
    KillTimer(TIMER_EDITOR_REPARSE);
    KillTimer(TIMER_CURSOR_BLINK);
    KillTimer(TIMER_NOTIFICATION);
    KillTimer(TIMER_ZOOM_APPLY);
    return 0;
}

// --- Public API ---

bool CMarkdownView::LoadFile(const std::wstring& path) {
    std::ifstream file(path);
    if (!file) return false;
    std::stringstream buffer;
    buffer << file.rdbuf();

    auto result = m_app.parser.parse(buffer.str());
    if (!result.success) return false;

    m_app.root = result.root;
    m_app.parseTimeUs = result.parseTimeUs;
    m_app.currentFile = toUtf8(path);
    m_app.layoutDirty = true;

    updateFileWriteTime(m_app);
    if (m_hWnd) {
        ::SetTimer(m_hWnd, TIMER_FILE_WATCH, 500, nullptr);
        updateWindowTitle(m_app);
        InvalidateRect(nullptr, FALSE);
    }
    return true;
}

bool CMarkdownView::LoadText(const std::wstring& content) {
    auto result = m_app.parser.parse(toUtf8(content));
    if (!result.success) return false;

    m_app.root = result.root;
    m_app.parseTimeUs = result.parseTimeUs;
    m_app.currentFile.clear();
    m_app.layoutDirty = true;

    if (m_hWnd) {
        updateWindowTitle(m_app);
        InvalidateRect(nullptr, FALSE);
    }
    return true;
}

void CMarkdownView::SetTheme(int themeIndex) {
    applyTheme(m_app, themeIndex);
}

void CMarkdownView::SetZoom(float factor) {
    factor = std::max(0.5f, std::min(3.0f, factor));
    if (factor == m_app.zoomFactor) return;
    m_app.zoomFactor = factor;
    updateTextFormats(m_app);  // also syncs appliedZoomFactor
    m_app.layoutDirty = true;
    if (m_hWnd) InvalidateRect(nullptr, FALSE);
    if (m_app.host) m_app.host->OnZoomChanged(m_app.zoomFactor);
}

void CMarkdownView::Find(const std::wstring& query) {
    m_app.showSearch = true;
    m_app.searchActive = true;
    m_app.searchAnimation = 0;
    m_app.searchJustOpened = false;
    m_app.searchQuery = query;
    m_app.searchMatches.clear();
    performSearch(m_app);
    m_app.searchCurrentIndex = 0;
    if (!m_app.searchMatches.empty()) {
        scrollToCurrentMatch(m_app);
    }
    if (m_hWnd) InvalidateRect(nullptr, FALSE);
}

void CMarkdownView::FindNext() {
    if (!m_app.showSearch || m_app.searchMatches.empty()) return;
    m_app.searchCurrentIndex = (m_app.searchCurrentIndex + 1) % (int)m_app.searchMatches.size();
    scrollToCurrentMatch(m_app);
    if (m_hWnd) InvalidateRect(nullptr, FALSE);
}

void CMarkdownView::FindPrev() {
    if (!m_app.showSearch || m_app.searchMatches.empty()) return;
    int count = (int)m_app.searchMatches.size();
    m_app.searchCurrentIndex = (m_app.searchCurrentIndex - 1 + count) % count;
    scrollToCurrentMatch(m_app);
    if (m_hWnd) InvalidateRect(nullptr, FALSE);
}

void CMarkdownView::CloseFind() {
    m_app.showSearch = false;
    m_app.searchActive = false;
    m_app.searchQuery.clear();
    m_app.searchMatches.clear();
    m_app.searchAnimation = 0;
    if (m_hWnd) InvalidateRect(nullptr, FALSE);
}

void CMarkdownView::EnterEditMode() {
    enterEditMode(m_app);
    if (m_hWnd) InvalidateRect(nullptr, FALSE);
}

void CMarkdownView::ExitEditMode() {
    exitEditMode(m_app);
    if (m_hWnd) InvalidateRect(nullptr, FALSE);
}

bool CMarkdownView::Save() {
    // editorText (what gets written) is only populated once edit mode has
    // been entered — see enterEditMode() in editor.cpp.
    if (!m_app.editMode) return false;
    saveEditorFile(m_app, m_hWnd);
    return !m_app.editorDirty;
}

void CMarkdownView::SetSettings(const Settings& settings) {
    m_app.currentThemeIndex = settings.themeIndex;
    m_app.theme = THEMES[settings.themeIndex];
    m_app.darkMode = m_app.theme.isDark;
    m_app.zoomFactor = settings.zoomFactor;
    if (m_hWnd) {
        updateTextFormats(m_app);
        m_app.layoutDirty = true;
        InvalidateRect(nullptr, FALSE);
    }
}

Settings CMarkdownView::GetSettings() const {
    Settings settings;
    settings.themeIndex = m_app.currentThemeIndex;
    settings.zoomFactor = m_app.zoomFactor;
    return settings;
}
