/****************************************************************************
** Meta object code from reading C++ file 'network_speed_tester.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.12.12)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../core/network_speed_tester.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'network_speed_tester.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.12.12. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_NetworkSpeedTester_t {
    QByteArrayData data[15];
    char stringdata0[186];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_NetworkSpeedTester_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_NetworkSpeedTester_t qt_meta_stringdata_NetworkSpeedTester = {
    {
QT_MOC_LITERAL(0, 0, 18), // "NetworkSpeedTester"
QT_MOC_LITERAL(1, 19, 12), // "testProgress"
QT_MOC_LITERAL(2, 32, 0), // ""
QT_MOC_LITERAL(3, 33, 7), // "message"
QT_MOC_LITERAL(4, 41, 12), // "testFinished"
QT_MOC_LITERAL(5, 54, 13), // "downloadSpeed"
QT_MOC_LITERAL(6, 68, 11), // "uploadSpeed"
QT_MOC_LITERAL(7, 80, 9), // "testError"
QT_MOC_LITERAL(8, 90, 5), // "error"
QT_MOC_LITERAL(9, 96, 14), // "onPingFinished"
QT_MOC_LITERAL(10, 111, 8), // "exitCode"
QT_MOC_LITERAL(11, 120, 20), // "QProcess::ExitStatus"
QT_MOC_LITERAL(12, 141, 10), // "exitStatus"
QT_MOC_LITERAL(13, 152, 19), // "onSpeedTestFinished"
QT_MOC_LITERAL(14, 172, 13) // "onTestTimeout"

    },
    "NetworkSpeedTester\0testProgress\0\0"
    "message\0testFinished\0downloadSpeed\0"
    "uploadSpeed\0testError\0error\0onPingFinished\0"
    "exitCode\0QProcess::ExitStatus\0exitStatus\0"
    "onSpeedTestFinished\0onTestTimeout"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_NetworkSpeedTester[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       6,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       3,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   44,    2, 0x06 /* Public */,
       4,    2,   47,    2, 0x06 /* Public */,
       7,    1,   52,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       9,    2,   55,    2, 0x08 /* Private */,
      13,    2,   60,    2, 0x08 /* Private */,
      14,    0,   65,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::Double, QMetaType::Double,    5,    6,
    QMetaType::Void, QMetaType::QString,    8,

 // slots: parameters
    QMetaType::Void, QMetaType::Int, 0x80000000 | 11,   10,   12,
    QMetaType::Void, QMetaType::Int, 0x80000000 | 11,   10,   12,
    QMetaType::Void,

       0        // eod
};

void NetworkSpeedTester::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<NetworkSpeedTester *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->testProgress((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 1: _t->testFinished((*reinterpret_cast< double(*)>(_a[1])),(*reinterpret_cast< double(*)>(_a[2]))); break;
        case 2: _t->testError((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 3: _t->onPingFinished((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< QProcess::ExitStatus(*)>(_a[2]))); break;
        case 4: _t->onSpeedTestFinished((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< QProcess::ExitStatus(*)>(_a[2]))); break;
        case 5: _t->onTestTimeout(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (NetworkSpeedTester::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&NetworkSpeedTester::testProgress)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (NetworkSpeedTester::*)(double , double );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&NetworkSpeedTester::testFinished)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (NetworkSpeedTester::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&NetworkSpeedTester::testError)) {
                *result = 2;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject NetworkSpeedTester::staticMetaObject = { {
    &QObject::staticMetaObject,
    qt_meta_stringdata_NetworkSpeedTester.data,
    qt_meta_data_NetworkSpeedTester,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *NetworkSpeedTester::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *NetworkSpeedTester::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_NetworkSpeedTester.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int NetworkSpeedTester::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 6;
    }
    return _id;
}

// SIGNAL 0
void NetworkSpeedTester::testProgress(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void NetworkSpeedTester::testFinished(double _t1, double _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void NetworkSpeedTester::testError(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
