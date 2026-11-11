/****************************************************************************
** Meta object code from reading C++ file 'illegal_shutdown_detector.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../core/illegal_shutdown_detector.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'illegal_shutdown_detector.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN23IllegalShutdownDetectorE_t {};
} // unnamed namespace

template <> constexpr inline auto IllegalShutdownDetector::qt_create_metaobjectdata<qt_meta_tag_ZN23IllegalShutdownDetectorE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "IllegalShutdownDetector",
        "illegalShutdownDetected",
        "",
        "shutdownTime",
        "logInfo",
        "illegalShutdownStateRestored",
        "illegalShutdownCountChanged",
        "count",
        "appCrashDetected",
        "checkOnStartup"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'illegalShutdownDetected'
        QtMocHelpers::SignalData<void(const QDateTime &, const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QDateTime, 3 }, { QMetaType::QString, 4 },
        }}),
        // Signal 'illegalShutdownStateRestored'
        QtMocHelpers::SignalData<void(const QDateTime &, const QString &)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QDateTime, 3 }, { QMetaType::QString, 4 },
        }}),
        // Signal 'illegalShutdownCountChanged'
        QtMocHelpers::SignalData<void(int)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 7 },
        }}),
        // Signal 'appCrashDetected'
        QtMocHelpers::SignalData<void(const QString &)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 4 },
        }}),
        // Slot 'checkOnStartup'
        QtMocHelpers::SlotData<void()>(9, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<IllegalShutdownDetector, qt_meta_tag_ZN23IllegalShutdownDetectorE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject IllegalShutdownDetector::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN23IllegalShutdownDetectorE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN23IllegalShutdownDetectorE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN23IllegalShutdownDetectorE_t>.metaTypes,
    nullptr
} };

void IllegalShutdownDetector::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<IllegalShutdownDetector *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->illegalShutdownDetected((*reinterpret_cast<std::add_pointer_t<QDateTime>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 1: _t->illegalShutdownStateRestored((*reinterpret_cast<std::add_pointer_t<QDateTime>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 2: _t->illegalShutdownCountChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 3: _t->appCrashDetected((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 4: _t->checkOnStartup(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (IllegalShutdownDetector::*)(const QDateTime & , const QString & )>(_a, &IllegalShutdownDetector::illegalShutdownDetected, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (IllegalShutdownDetector::*)(const QDateTime & , const QString & )>(_a, &IllegalShutdownDetector::illegalShutdownStateRestored, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (IllegalShutdownDetector::*)(int )>(_a, &IllegalShutdownDetector::illegalShutdownCountChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (IllegalShutdownDetector::*)(const QString & )>(_a, &IllegalShutdownDetector::appCrashDetected, 3))
            return;
    }
}

const QMetaObject *IllegalShutdownDetector::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *IllegalShutdownDetector::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN23IllegalShutdownDetectorE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int IllegalShutdownDetector::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 5;
    }
    return _id;
}

// SIGNAL 0
void IllegalShutdownDetector::illegalShutdownDetected(const QDateTime & _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}

// SIGNAL 1
void IllegalShutdownDetector::illegalShutdownStateRestored(const QDateTime & _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2);
}

// SIGNAL 2
void IllegalShutdownDetector::illegalShutdownCountChanged(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void IllegalShutdownDetector::appCrashDetected(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}
QT_WARNING_POP
