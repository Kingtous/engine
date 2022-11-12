// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/cursor_handler.h"

#include <windows.h>

#include "flutter/shell/platform/common/client_wrapper/include/flutter/standard_method_codec.h"

static constexpr char kChannelName[] = "flutter/mousecursor";

static constexpr char kActivateSystemCursorMethod[] = "activateSystemCursor";
static constexpr char kSetSystemCursorMethod[] = "setSystemCursor";

static constexpr char kKindKey[] = "kind";

namespace flutter {

CursorHandler::CursorHandler(BinaryMessenger* messenger,
                             WindowBindingHandler* delegate)
    : channel_(std::make_unique<MethodChannel<EncodableValue>>(
          messenger,
          kChannelName,
          &StandardMethodCodec::GetInstance())),
      delegate_(delegate) {
  channel_->SetMethodCallHandler(
      [this](const MethodCall<EncodableValue>& call,
             std::unique_ptr<MethodResult<EncodableValue>> result) {
        HandleMethodCall(call, std::move(result));
      });
}

void GetMaskBitmaps(HBITMAP hSourceBitmap,
                    COLORREF clrTransparent,
                    HBITMAP& hAndMaskBitmap,
                    HBITMAP& hXorMaskBitmap) {
  HDC hDC = ::GetDC(NULL);
  HDC hMainDC = ::CreateCompatibleDC(hDC);
  HDC hAndMaskDC = ::CreateCompatibleDC(hDC);
  HDC hXorMaskDC = ::CreateCompatibleDC(hDC);

  // Get the dimensions of the source bitmap
  BITMAP bm;
  ::GetObject(hSourceBitmap, sizeof(BITMAP), &bm);
  hAndMaskBitmap = ::CreateCompatibleBitmap(hDC, bm.bmWidth, bm.bmHeight);
  hXorMaskBitmap = ::CreateCompatibleBitmap(hDC, bm.bmWidth, bm.bmHeight);

  // Select the bitmaps to DC
  HBITMAP hOldMainBitmap = (HBITMAP)::SelectObject(hMainDC, hSourceBitmap);
  HBITMAP hOldAndMaskBitmap =
      (HBITMAP)::SelectObject(hAndMaskDC, hAndMaskBitmap);
  HBITMAP hOldXorMaskBitmap =
      (HBITMAP)::SelectObject(hXorMaskDC, hXorMaskBitmap);

  // Scan each pixel of the souce bitmap and create the masks
  COLORREF MainBitPixel;
  for (int x = 0; x < bm.bmWidth; ++x) {
    for (int y = 0; y < bm.bmHeight; ++y) {
      MainBitPixel = ::GetPixel(hMainDC, x, y);
      if (MainBitPixel == clrTransparent) {
        ::SetPixel(hAndMaskDC, x, y, RGB(255, 255, 255));
        ::SetPixel(hXorMaskDC, x, y, RGB(0, 0, 0));
      } else {
        ::SetPixel(hAndMaskDC, x, y, RGB(0, 0, 0));
        ::SetPixel(hXorMaskDC, x, y, MainBitPixel);
      }
    }
  }
  ::SelectObject(hMainDC, hOldMainBitmap);
  ::SelectObject(hAndMaskDC, hOldAndMaskBitmap);
  ::SelectObject(hXorMaskDC, hOldXorMaskBitmap);

  ::DeleteDC(hXorMaskDC);
  ::DeleteDC(hAndMaskDC);
  ::DeleteDC(hMainDC);

  ::ReleaseDC(NULL, hDC);
}

void CursorHandler::HandleMethodCall(
    const MethodCall<EncodableValue>& method_call,
    std::unique_ptr<MethodResult<EncodableValue>> result) {
  const std::string& method = method_call.method_name();
  if (method.compare(kActivateSystemCursorMethod) == 0) {
    const auto& arguments = std::get<EncodableMap>(*method_call.arguments());
    auto kind_iter = arguments.find(EncodableValue(std::string(kKindKey)));
    if (kind_iter == arguments.end()) {
      result->Error("Argument error",
                    "Missing argument while trying to activate system cursor");
      return;
    }
    const auto& kind = std::get<std::string>(kind_iter->second);
    delegate_->UpdateFlutterCursor(kind);
    result->Success();
  } else if (method.compare(kSetSystemCursorMethod) == 0) {
    const auto& map = std::get<EncodableMap>(*method_call.arguments());
    auto buffer = std::get<std::vector<uint8_t>>(
        map.at(flutter::EncodableValue("buffer")));
    auto key = std::get<std::string>(map.at(flutter::EncodableValue("key")));
    auto scale_x = std::get<int>(map.at(flutter::EncodableValue("scale_x")));
    auto scale_y = std::get<int>(map.at(flutter::EncodableValue("scale_y")));
    auto x = std::get<double>(map.at(flutter::EncodableValue("x")));
    auto y = std::get<double>(map.at(flutter::EncodableValue("y")));
    auto length = std::get<int>(map.at(flutter::EncodableValue("length")));
    HCURSOR cursor = nullptr;
    // 32-> argb
    HDC display_dc = GetDC(0);
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = scale_x;
    bmi.bmiHeader.biHeight = -scale_y;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biSizeImage = scale_x * scale_y * 4;
    // Create the pixmap
    uint8_t* pixels = 0;
    HBITMAP bitmap = CreateDIBSection(display_dc, &bmi, DIB_RGB_COLORS,
                                      (void**)&pixels, 0, 0);
    ReleaseDC(0, display_dc);
    if (!bitmap) {
      result->Error("bitmap error", "create dib section failed");
      return;
    }
    if (!pixels) {
      result->Error("bitmap error", "did not allocate pixel data");
      return;
    }
    int bytes_per_line = scale_x * 4;
    for (int y = 0; y < scale_y; ++y)
      memcpy(pixels + y * bytes_per_line, &buffer[bytes_per_line * y],
             bytes_per_line);

    // auto bitmap = CreateBitmap(scale_x, scale_y, 1, 32, &buffer[0]);
    if (bitmap == nullptr) {
      result->Error("Argument error", "Invalid rawRgba bitmap from flutter");
      return;
    }
    HBITMAP andMask;
    HBITMAP xorMask;
    GetMaskBitmaps(bitmap, RGB(0, 0, 0), andMask, xorMask);
    DeleteObject(bitmap);
    ICONINFO ii;
    ii.fIcon = 0;
    ii.xHotspot = x;
    ii.yHotspot = y;
    ii.hbmMask = andMask;
    ii.hbmColor = xorMask;
    cursor = CreateIconIndirect(&ii);
    DeleteObject(andMask);
    DeleteObject(xorMask);
    delegate_->SetFlutterCursor(cursor);
    result->Success();
  } else {
    result->NotImplemented();
  }
}

}  // namespace flutter
