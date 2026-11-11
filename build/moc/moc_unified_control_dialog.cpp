/****************************************************************************
** Meta object code from reading C++ file 'unified_control_dialog.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../dialogs/unified_control_dialog.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'unified_control_dialog.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN20UnifiedControlDialogE_t {};
} // unnamed namespace

template <> constexpr inline auto UnifiedControlDialog::qt_create_metaobjectdata<qt_meta_tag_ZN20UnifiedControlDialogE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "UnifiedControlDialog",
        "executeCommand",
        "",
        "command",
        "params",
        "onExecute",
        "onRefreshPeers",
        "onCycleCheckChanged",
        "state",
        "onSelectAllPeers",
        "onConnectServerCheckChanged"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'executeCommand'
        QtMocHelpers::SignalData<void(const QString &, const QStringList &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 }, { QMetaType::QStringList, 4 },
        }}),
        // Slot 'onExecute'
        QtMocHelpers::SlotData<void()>(5, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onRefreshPeers'
        QtMocHelpers::SlotData<void()>(6, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onCycleCheckChanged'
        QtMocHelpers::SlotData<void(int)>(7, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 8 },
        }}),
        // Slot 'onSelectAllPeers'
        QtMocHelpers::SlotData<void(int)>(9, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 8 },
        }}),
        // Slot 'onConnectServerCheckChanged'
        QtMocHelpers::SlotData<void(int)>(10, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 8 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<UnifiedControlDialog, qt_meta_tag_ZN20UnifiedControlDialogE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject UnifiedControlDialog::staticMetaObject = { {
    QMetaObject::SuperData::link<QDialog::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN20UnifiedControlDialogE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN20UnifiedControlDialogE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN20UnifiedControlDialogE_t>.metaTypes,
    nullptr
} };

void UnifiedControlDialog::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<UnifiedControlDialog *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->executeCommand((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[2]))); break;
        case 1: _t->onExecute(); break;
        case 2: _t->onRefreshPeers(); break;
        case 3: _t->onCycleCheckChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 4: _t->onSelectAllPeers((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 5: _t->onConnectServerCheckChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (UnifiedControlDialog::*)(const QString & , const QStringList & )>(_a, &UnifiedControlDialog::executeCommand, 0))
            return;
    }
}

const QMetaObject *UnifiedControlDialog::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *UnifiedControlDialog::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN20UnifiedControlDialogE_t>.strings))
        return static_cast<void*>(this);
    return QDialog::qt_metacast(_clname);
}

int UnifiedControlDialog::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QDialog::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 6;
    }
    return _id;
}

// SIGNAL 0
void UnifiedControlDialog::executeCommand(const QString & _t1, const QStringList & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}
QT_WARNING_POP
