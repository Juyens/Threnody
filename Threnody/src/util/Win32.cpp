#include "util/Win32.h"

namespace threnody::win32 {

WindowClass::WindowClass(const WNDCLASSEXW& description)
    : m_instance(description.hInstance),
      m_name(description.lpszClassName),
      m_atom(RegisterClassExW(&description)) {}

WindowClass::~WindowClass() {
    if (m_atom != 0) {
        UnregisterClassW(m_name, m_instance);
    }
}

}  // namespace threnody::win32
