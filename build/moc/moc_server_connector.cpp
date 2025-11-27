/****************************************************************************
** Meta object code from reading C++ file 'server_connector.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.12.12)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../core/server_connector.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'server_connector.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.12.12. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_ServerConnector_t {
    QByteArrayData data[15];
    char stringdata0[207];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_ServerConnector_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_ServerConnector_t qt_meta_stringdata_ServerConnector = {
    {
QT_MOC_LITERAL(0, 0, 15), // "ServerConnector"
QT_MOC_LITERAL(1, 16, 23), // "connectionStatusChanged"
QT_MOC_LITERAL(2, 40, 0), // ""
QT_MOC_LITERAL(3, 41, 9), // "connected"
QT_MOC_LITERAL(4, 51, 15), // "messageReceived"
QT_MOC_LITERAL(5, 67, 7), // "message"
QT_MOC_LITERAL(6, 75, 10), // "logMessage"
QT_MOC_LITERAL(7, 86, 11), // "onConnected"
QT_MOC_LITERAL(8, 98, 14), // "onDisconnected"
QT_MOC_LITERAL(9, 113, 11), // "onReadyRead"
QT_MOC_LITERAL(10, 125, 15), // "onErrorOccurred"
QT_MOC_LITERAL(11, 141, 28), // "QAbstractSocket::SocketError"
QT_MOC_LITERAL(12, 170, 5), // "error"
QT_MOC_LITERAL(13, 176, 22), // "onServerConnectToggled"
QT_MOC_LITERAL(14, 199, 7) // "checked"

    },
    "ServerConnector\0connectionStatusChanged\0"
    "\0connected\0messageReceived\0message\0"
    "logMessage\0onConnected\0onDisconnected\0"
    "onReadyRead\0onErrorOccurred\0"
    "QAbstractSocket::SocketError\0error\0"
    "onServerConnectToggled\0checked"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_ServerConnector[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       8,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       3,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   54,    2, 0x06 /* Public */,
       4,    1,   57,    2, 0x06 /* Public */,
       6,    1,   60,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       7,    0,   63,    2, 0x08 /* Private */,
       8,    0,   64,    2, 0x08 /* Private */,
       9,    0,   65,    2, 0x08 /* Private */,
      10,    1,   66,    2, 0x08 /* Private */,
      13,    1,   69,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::Bool,    3,
    QMetaType::Void, QMetaType::QString,    5,
    QMetaType::Void, QMetaType::QString,    5,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 11,   12,
    QMetaType::Void, QMetaType::Bool,   14,

       0        // eod
};

void ServerConnector::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<ServerConnector *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->connectionStatusChanged((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 1: _t->messageReceived((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 2: _t->logMessage((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 3: _t->onConnected(); break;
        case 4: _t->onDisconnected(); break;
        case 5: _t->onReadyRead(); break;
        case 6: _t->onErrorOccurred((*reinterpret_cast< QAbstractSocket::SocketError(*)>(_a[1]))); break;
        case 7: _t->onServerConnectToggled((*reinterpret_cast< bool(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 6:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< QAbstractSocket::SocketError >(); break;
            }
            break;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (ServerConnector::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ServerConnector::connectionStatusChanged)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (ServerConnector::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ServerConnector::messageReceived)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (ServerConnector::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ServerConnector::logMessage)) {
                *result = 2;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject ServerConnector::staticMetaObject = { {
    &QObject::staticMetaObject,
    qt_meta_stringdata_ServerConnector.data,
    qt_meta_data_ServerConnector,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *ServerConnector::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ServerConnector::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ServerConnector.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int ServerConnector::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
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
            qt_static_metacall(this, _c, _id, _a);
        _id -= 8;
    }
    return _id;
}

// SIGNAL 0
void ServerConnector::connectionStatusChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void ServerConnector::messageReceived(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void ServerConnector::logMessage(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
