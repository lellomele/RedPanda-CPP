#ifndef REDPANDA_APPIDENTITY_H
#define REDPANDA_APPIDENTITY_H
#include <QString>
namespace AppIdentity {
inline QString displayName() { return QStringLiteral("Red Panda C++"); }
inline QString forkCopyright() { return QStringLiteral("© 2006 Ing. prof. Raffaele Mele"); }
inline QString internalName() { return QStringLiteral("RedPandaIDE"); }
inline QString iconResource() { return QStringLiteral(":/icons/app/redpanda-cpp.png"); }
}
#endif
