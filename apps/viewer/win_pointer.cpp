#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "otk/input.hpp"

namespace {

WNDPROC original = nullptr;
otk::InputController* target = nullptr;

LRESULT CALLBACK pointerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (target && (msg == WM_POINTERDOWN || msg == WM_POINTERUPDATE || msg == WM_POINTERUP || msg == WM_POINTERCAPTURECHANGED)) {
        UINT32 id = GET_POINTERID_WPARAM(wParam);
        POINTER_INPUT_TYPE type = PT_POINTER;
        if (GetPointerType(id, &type) && (type == PT_TOUCH || type == PT_PEN)) {
            int key = static_cast<int>(id) + 100000;
            if (msg == WM_POINTERCAPTURECHANGED) {
                target->pointerCancel(key);
                return 0;
            }
            POINTER_INFO info{};
            if (GetPointerInfo(id, &info)) {
                POINT pt = info.ptPixelLocation;
                ScreenToClient(hwnd, &pt);
                double x = pt.x, y = pt.y;
                otk::PointerType pt2 = type == PT_PEN ? otk::PointerType::Pen : otk::PointerType::Touch;
                if (msg == WM_POINTERDOWN) target->pointerDown(key, x, y, pt2);
                else if (msg == WM_POINTERUPDATE) target->pointerMove(key, x, y);
                else target->pointerUp(key, x, y);
            }
            return 0;
        }
    }
    return CallWindowProcW(original, hwnd, msg, wParam, lParam);
}

}

void installPointerInput(const void* hwnd, otk::InputController* input) {
    HWND h = static_cast<HWND>(const_cast<void*>(hwnd));
    if (!h || original) return;
    target = input;
    original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(pointerProc)));
}
#endif
