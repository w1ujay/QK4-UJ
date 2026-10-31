#include "update/windowsappid.h"

#ifdef Q_OS_WIN

#include <QDebug>
#include <QDir>
#include <QWidget>
#include <cstring>
#include <string>

// WHY last: see the note in main.cpp. windows.h rewrites unqualified names in anything included after it.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <initguid.h>
#include <objbase.h>
#include <propidl.h>
#include <propkey.h>
#include <shellapi.h>
#include <shobjidl.h>

namespace {

// Must never change: Windows groups the taskbar button and finds existing pins by it.
const wchar_t APP_USER_MODEL_ID[] = L"QK4UJ.QK4";
const wchar_t DISPLAY_NAME[] = L"QK4";

bool setStringProperty(IPropertyStore *store, const PROPERTYKEY &key, const std::wstring &value) {
    const size_t bytes = (value.size() + 1) * sizeof(wchar_t);
    auto *copy = static_cast<wchar_t *>(CoTaskMemAlloc(bytes));
    if (!copy)
        return false;
    memcpy(copy, value.c_str(), bytes);

    PROPVARIANT variant;
    PropVariantInit(&variant);
    variant.vt = VT_LPWSTR;
    variant.pwszVal = copy; // PropVariantClear frees it
    const HRESULT result = store->SetValue(key, variant);
    PropVariantClear(&variant);
    return SUCCEEDED(result);
}

} // namespace

void applyWindowsRelaunchIdentity(QWidget *window, const QString &launcherPath) {
    if (!window)
        return;
    IPropertyStore *store = nullptr;
    const HRESULT result = SHGetPropertyStoreForWindow(reinterpret_cast<HWND>(window->winId()), IID_PPV_ARGS(&store));
    if (FAILED(result)) {
        qWarning() << "Update: no window property store, error" << Qt::hex << ulong(result);
        return;
    }

    // Quoted, because an install path can hold spaces.
    const std::wstring command = L"\"" + QDir::toNativeSeparators(launcherPath).toStdWString() + L"\"";
    const bool ok = setStringProperty(store, PKEY_AppUserModel_ID, APP_USER_MODEL_ID) &&
                    setStringProperty(store, PKEY_AppUserModel_RelaunchCommand, command) &&
                    setStringProperty(store, PKEY_AppUserModel_RelaunchDisplayNameResource, DISPLAY_NAME) &&
                    SUCCEEDED(store->Commit());
    store->Release();
    if (!ok)
        qWarning() << "Update: could not set the taskbar relaunch command";
}

#else

void applyWindowsRelaunchIdentity(QWidget *, const QString &) {}

#endif
