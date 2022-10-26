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
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = scale_x;
    bmi.bmiHeader.biHeight      = -scale_y;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biSizeImage   = scale_x * scale_y * 4;
    // Create the pixmap
    uint8_t *pixels = 0;
    HBITMAP bitmap = CreateDIBSection(display_dc, &bmi, DIB_RGB_COLORS, (void **) &pixels, 0, 0);
    ReleaseDC(0, display_dc);
    if (!bitmap) {
        result->Error("bitmap error","create dib section failed");
        return;
    }
    if (!pixels) {
        result->Error("bitmap error","did not allocate pixel data");
        return;
    }
    int bytes_per_line = scale_x * 4;
    for (int y=0; y<scale_y; ++y)
      memcpy(pixels + y * bytes_per_line, &buffer[bytes_per_line * y] , bytes_per_line);

    // auto bitmap = CreateBitmap(scale_x, scale_y, 1, 32, &buffer[0]);
    if (bitmap == nullptr) {
      result->Error("Argument error", "Invalid rawRgba bitmap from flutter");
      return;
    }
    auto len = scale_x*scale_y;
    uint8_t* bits = new uint8_t[len * 4];
    for(auto i =0; i<len;i++){
      auto base = i*4; // rgba
      if (buffer[base] > 0 || buffer[base+1] >0 || buffer[base + 2] > 0 || buffer[base + 3] > 0) {
        bits[base] = 0xFF;
        bits[base + 1] = 0xFF;
        bits[base + 2] = 0xFF;
        bits[base + 3] = 0xFF;
      } else {
        bits[base] = 0x0;
        bits[base + 1] = 0x0;
        bits[base + 2] = 0x0;
        bits[base + 3] = 0x0;
      }
    }
    HBITMAP mask = CreateBitmap(scale_x, scale_y, 1, 32, bits);
    ICONINFO ii;
    ii.fIcon = 0;
    ii.xHotspot = x;
    ii.yHotspot = y;
    ii.hbmMask = mask;
    ii.hbmColor = bitmap;
    cursor = CreateIconIndirect(&ii);
    delegate_->SetFlutterCursor(cursor);
    delete[] bits;
    result->Success();
  } else {
    result->NotImplemented();
  }
}

}  // namespace flutter
