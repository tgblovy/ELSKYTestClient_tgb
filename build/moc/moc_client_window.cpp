/****************************************************************************
** Meta object code from reading C++ file 'client_window.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../core/client_window.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'client_window.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.11.1. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN12ClientWindowE_t {};
} // unnamed namespace

template <> constexpr inline auto ClientWindow::qt_create_metaobjectdata<qt_meta_tag_ZN12ClientWindowE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ClientWindow",
        "onDeviceChanged",
        "",
        "message",
        "onUsbDeviceChanged",
        "action",
        "deviceInfo",
        "onInputDeviceChanged",
        "onDisplayChanged",
        "displayInfo",
        "toggleAlwaysOnTop",
        "on",
        "onCycleCompleted",
        "clearDeviceInfoDisplay",
        "onIllegalShutdownDetected",
        "shutdownTime",
        "logInfo",
        "onIllegalShutdownStateRestored",
        "showIllegalShutdownLog",
        "onRecheckIllegalShutdown",
        "onClearIllegalShutdown",
        "onUnifiedControl",
        "onUnifiedCommandReceived",
        "command",
        "params",
        "senderIP",
        "onTagChanged",
        "index",
        "onAddTag",
        "onManageTags",
        "showDeviceChangeLog"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'onDeviceChanged'
        QtMocHelpers::SlotData<void(const QString &)>(1, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Slot 'onUsbDeviceChanged'
        QtMocHelpers::SlotData<void(const QString &, const QString &)>(4, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 5 }, { QMetaType::QString, 6 },
        }}),
        // Slot 'onInputDeviceChanged'
        QtMocHelpers::SlotData<void(const QString &, const QString &)>(7, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 5 }, { QMetaType::QString, 6 },
        }}),
        // Slot 'onDisplayChanged'
        QtMocHelpers::SlotData<void(const QString &, const QString &)>(8, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 5 }, { QMetaType::QString, 9 },
        }}),
        // Slot 'toggleAlwaysOnTop'
        QtMocHelpers::SlotData<void(bool)>(10, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 11 },
        }}),
        // Slot 'onCycleCompleted'
        QtMocHelpers::SlotData<void()>(12, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'clearDeviceInfoDisplay'
        QtMocHelpers::SlotData<void()>(13, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onIllegalShutdownDetected'
        QtMocHelpers::SlotData<void(const QDateTime &, const QString &)>(14, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QDateTime, 15 }, { QMetaType::QString, 16 },
        }}),
        // Slot 'onIllegalShutdownStateRestored'
        QtMocHelpers::SlotData<void(const QDateTime &, const QString &)>(17, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QDateTime, 15 }, { QMetaType::QString, 16 },
        }}),
        // Slot 'showIllegalShutdownLog'
        QtMocHelpers::SlotData<void()>(18, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onRecheckIllegalShutdown'
        QtMocHelpers::SlotData<void()>(19, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onClearIllegalShutdown'
        QtMocHelpers::SlotData<void()>(20, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onUnifiedControl'
        QtMocHelpers::SlotData<void()>(21, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onUnifiedCommandReceived'
        QtMocHelpers::SlotData<void(const QString &, const QStringList &, const QString &)>(22, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 23 }, { QMetaType::QStringList, 24 }, { QMetaType::QString, 25 },
        }}),
        // Slot 'onTagChanged'
        QtMocHelpers::SlotData<void(int)>(26, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 27 },
        }}),
        // Slot 'onAddTag'
        QtMocHelpers::SlotData<void()>(28, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onManageTags'
        QtMocHelpers::SlotData<void()>(29, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'showDeviceChangeLog'
        QtMocHelpers::SlotData<void()>(30, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<ClientWindow, qt_meta_tag_ZN12ClientWindowE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ClientWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12ClientWindowE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12ClientWindowE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN12ClientWindowE_t>.metaTypes,
    nullptr
} };

void ClientWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<ClientWindow *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->onDeviceChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->onUsbDeviceChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 2: _t->onInputDeviceChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 3: _t->onDisplayChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 4: _t->toggleAlwaysOnTop((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 5: _t->onCycleCompleted(); break;
        case 6: _t->clearDeviceInfoDisplay(); break;
        case 7: _t->onIllegalShutdownDetected((*reinterpret_cast<std::add_pointer_t<QDateTime>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 8: _t->onIllegalShutdownStateRestored((*reinterpret_cast<std::add_pointer_t<QDateTime>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 9: _t->showIllegalShutdownLog(); break;
        case 10: _t->onRecheckIllegalShutdown(); break;
        case 11: _t->onClearIllegalShutdown(); break;
        case 12: _t->onUnifiedControl(); break;
        case 13: _t->onUnifiedCommandReceived((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        case 14: _t->onTagChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 15: _t->onAddTag(); break;
        case 16: _t->onManageTags(); break;
        case 17: _t->showDeviceChangeLog(); break;
        default: ;
        }
    }
}

const QMetaObject *ClientWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ClientWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12ClientWindowE_t>.strings))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int ClientWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 18)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 18;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 18)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 18;
    }
    return _id;
}
QT_WARNING_POP
