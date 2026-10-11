#ifndef REDPANDA_APPIDENTITY_H
#define REDPANDA_APPIDENTITY_H
#include <QString>
namespace AppIdentity {
inline QString displayName() { return QStringLiteral("Red Panda C++"); }
inline QString forkCopyright() { return QStringLiteral("Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)"); }
inline QString website() { return QStringLiteral("https://github.com/lellomele/RedPanda-CPP"); }
inline QString internalName() { return QStringLiteral("RedPandaIDE"); }
inline QString iconResource() { return QStringLiteral(":/icons/app/redpanda-cpp.png"); }
}
#endif
