/****************************************************************************
** Meta object code from reading C++ file 'basic_tester.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.12.12)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../core/basic_tester.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'basic_tester.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.12.12. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_BasicTester_t {
    QByteArrayData data[11];
    char stringdata0[111];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_BasicTester_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_BasicTester_t qt_meta_stringdata_BasicTester = {
    {
QT_MOC_LITERAL(0, 0, 11), // "BasicTester"
QT_MOC_LITERAL(1, 12, 10), // "logMessage"
QT_MOC_LITERAL(2, 23, 0), // ""
QT_MOC_LITERAL(3, 24, 7), // "message"
QT_MOC_LITERAL(4, 32, 11), // "executeTest"
QT_MOC_LITERAL(5, 44, 10), // "onShutdown"
QT_MOC_LITERAL(6, 55, 8), // "onReboot"
QT_MOC_LITERAL(7, 64, 9), // "onSuspend"
QT_MOC_LITERAL(8, 74, 11), // "onHibernate"
QT_MOC_LITERAL(9, 86, 9), // "onComTest"
QT_MOC_LITERAL(10, 96, 14) // "onUsbSpeedTest"

    },
    "BasicTester\0logMessage\0\0message\0"
    "executeTest\0onShutdown\0onReboot\0"
    "onSuspend\0onHibernate\0onComTest\0"
    "onUsbSpeedTest"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_BasicTester[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       8,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   54,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       4,    0,   57,    2, 0x0a /* Public */,
       5,    0,   58,    2, 0x08 /* Private */,
       6,    0,   59,    2, 0x08 /* Private */,
       7,    0,   60,    2, 0x08 /* Private */,
       8,    0,   61,    2, 0x08 /* Private */,
       9,    0,   62,    2, 0x08 /* Private */,
      10,    0,   63,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void BasicTester::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<BasicTester *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->logMessage((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 1: _t->executeTest(); break;
        case 2: _t->onShutdown(); break;
        case 3: _t->onReboot(); break;
        case 4: _t->onSuspend(); break;
        case 5: _t->onHibernate(); break;
        case 6: _t->onComTest(); break;
        case 7: _t->onUsbSpeedTest(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (BasicTester::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&BasicTester::logMessage)) {
                *result = 0;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject BasicTester::staticMetaObject = { {
    &QObject::staticMetaObject,
    qt_meta_stringdata_BasicTester.data,
    qt_meta_data_BasicTester,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *BasicTester::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *BasicTester::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_BasicTester.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int BasicTester::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 8)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 8;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 8)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 8;
    }
    return _id;
}

// SIGNAL 0
void BasicTester::logMessage(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
