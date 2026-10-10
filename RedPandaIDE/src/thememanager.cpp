/*
 * Copyright (C) 2020-2022 Roy Qu (royqh1979@gmail.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "thememanager.h"
#include <QApplication>
#include <QDirIterator>
#include <QFile>
#include <QSaveFile>
#include <QDateTime>
#include <QLocale>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>
#include <QMetaObject>
#include <QStyle>
#include "utils.h"
#include "settings.h"
#include "systemconsts.h"

#ifdef ENABLE_LUA_ADDON
#include "addon/luaexecutor.h"
#include "addon/luaruntime.h"
#endif

ThemeManager::ThemeManager(QObject *parent) : QObject(parent)
{

}

PAppTheme ThemeManager::theme(const QString &themeName)
{
    QString customThemeDir = pSettings->dirs().config(DirSettings::DataType::Theme);
    QString builtInThemeDir = pSettings->dirs().data(DirSettings::DataType::Theme);
    PAppTheme appTheme = nullptr;

    // custom overrides built-in
    if (tryLoadThemeFromDir(customThemeDir, AppTheme::ThemeCategory::Custom, themeName, appTheme))
        return appTheme;
    if (tryLoadThemeFromDir(builtInThemeDir, AppTheme::ThemeCategory::BuiltIn, themeName, appTheme))
        return appTheme;
    return AppTheme::fallbackTheme();
}

QList<PAppTheme> ThemeManager::getThemes()
{
    QString customThemeDir = pSettings->dirs().config(DirSettings::DataType::Theme);
    QString builtInThemeDir = pSettings->dirs().data(DirSettings::DataType::Theme);
    std::set<PAppTheme, ThemeCompare> themes;

    // custom overrides built-in
    loadThemesFromDir(customThemeDir, AppTheme::ThemeCategory::Custom, themes);
    loadThemesFromDir(builtInThemeDir, AppTheme::ThemeCategory::BuiltIn, themes);

    QList<PAppTheme> result(themes.begin(), themes.end());
    return result;
}

bool ThemeManager::ThemeCompare::operator()(const PAppTheme &lhs, const PAppTheme &rhs) const
{
    return QFileInfo(lhs->filename()).baseName() < QFileInfo(rhs->filename()).baseName();
}

bool ThemeManager::tryLoadThemeFromDir(const QString &dir, AppTheme::ThemeCategory category, const QString &themeName, PAppTheme &theme)
{
    for (const auto &[extension, type] : searchTypes) {
        QString filename = QString("%1.%2").arg(themeName, extension);
        QString fullPath = QString("%1/%2").arg(dir, filename);
        if (QFile::exists(fullPath)) {
            try {
                theme = loadCached(fullPath, type, category);
                return true;
            } catch(const FileError &e) {
                qWarning() << e.reason();
            }
#ifdef ENABLE_LUA_ADDON
            catch(AddOn::LuaError e) {
                qDebug() << e.reason();
            }
#endif
        }
    }
    return false;
}

void ThemeManager::loadThemesFromDir(const QString &dir, AppTheme::ThemeCategory category, std::set<PAppTheme, ThemeCompare> &themes)
{
    for (const auto &[extension, type] : searchTypes) {
        QDirIterator it(dir);
        while (it.hasNext()) {
            it.next();
            QFileInfo fileInfo = it.fileInfo();
            if (fileInfo.suffix().compare(extension, PATH_SENSITIVITY) == 0) {
                try {
                    PAppTheme appTheme = loadCached(fileInfo.absoluteFilePath(), type, category);
                    themes.insert(appTheme);
                } catch(const FileError &e) {
                    qWarning() << e.reason();
                }
#ifdef ENABLE_LUA_ADDON
                catch(AddOn::LuaError e) {
                    qDebug() << e.reason();
                }
#endif
            }
        }
    }
}

QColor AppTheme::color(ColorRole role) const
{
    return mColors.value(role,QColor());
}

QPalette AppTheme::palette() const
{
    QPalette pal = initialPalette();

    const static struct {
        ColorRole themeColor;
        QPalette::ColorRole paletteColorRole;
        QPalette::ColorGroup paletteColorGroup;
        bool setColorRoleAsBrush;
    } mapping[] = {
        {ColorRole::PaletteWindow,                    QPalette::Window,           QPalette::All,      false},
        {ColorRole::PaletteWindowDisabled,            QPalette::Window,           QPalette::Disabled, false},
        {ColorRole::PaletteWindowText,                QPalette::WindowText,       QPalette::All,      true},
        {ColorRole::PaletteWindowTextDisabled,        QPalette::WindowText,       QPalette::Disabled, true},
        {ColorRole::PaletteBase,                      QPalette::Base,             QPalette::All,      false},
        {ColorRole::PaletteBaseDisabled,              QPalette::Base,             QPalette::Disabled, false},
        {ColorRole::PaletteAlternateBase,             QPalette::AlternateBase,    QPalette::All,      false},
        {ColorRole::PaletteAlternateBaseDisabled,     QPalette::AlternateBase,    QPalette::Disabled, false},
        {ColorRole::PaletteToolTipBase,               QPalette::ToolTipBase,      QPalette::All,      true},
        {ColorRole::PaletteToolTipBaseDisabled,       QPalette::ToolTipBase,      QPalette::Disabled, true},
        {ColorRole::PaletteToolTipText,               QPalette::ToolTipText,      QPalette::All,      false},
        {ColorRole::PaletteToolTipTextDisabled,       QPalette::ToolTipText,      QPalette::Disabled, false},
        {ColorRole::PaletteText,                      QPalette::Text,             QPalette::All,      true},
        {ColorRole::PaletteTextDisabled,              QPalette::Text,             QPalette::Disabled, true},
        {ColorRole::PaletteButton,                    QPalette::Button,           QPalette::All,      false},
        {ColorRole::PaletteButtonDisabled,            QPalette::Button,           QPalette::Disabled, false},
        {ColorRole::PaletteButtonText,                QPalette::ButtonText,       QPalette::All,      true},
        {ColorRole::PaletteButtonTextDisabled,        QPalette::ButtonText,       QPalette::Disabled, true},
        {ColorRole::PaletteBrightText,                QPalette::BrightText,       QPalette::All,      false},
        {ColorRole::PaletteBrightTextDisabled,        QPalette::BrightText,       QPalette::Disabled, false},
        {ColorRole::PaletteHighlight,                 QPalette::Highlight,        QPalette::All,      true},
        {ColorRole::PaletteHighlightDisabled,         QPalette::Highlight,        QPalette::Disabled, true},
        {ColorRole::PaletteHighlightedText,           QPalette::HighlightedText,  QPalette::All,      true},
        {ColorRole::PaletteHighlightedTextDisabled,   QPalette::HighlightedText,  QPalette::Disabled, true},
        {ColorRole::PaletteLink,                      QPalette::Link,             QPalette::All,      false},
        {ColorRole::PaletteLinkDisabled,              QPalette::Link,             QPalette::Disabled, false},
        {ColorRole::PaletteLinkVisited,               QPalette::LinkVisited,      QPalette::All,      false},
        {ColorRole::PaletteLinkVisitedDisabled,       QPalette::LinkVisited,      QPalette::Disabled, false},
        {ColorRole::PaletteLight,                     QPalette::Light,            QPalette::All,      false},
        {ColorRole::PaletteLightDisabled,             QPalette::Light,            QPalette::Disabled, false},
        {ColorRole::PaletteMidlight,                  QPalette::Midlight,         QPalette::All,      false},
        {ColorRole::PaletteMidlightDisabled,          QPalette::Midlight,         QPalette::Disabled, false},
        {ColorRole::PaletteDark,                      QPalette::Dark,             QPalette::All,      false},
        {ColorRole::PaletteDarkDisabled,              QPalette::Dark,             QPalette::Disabled, false},
        {ColorRole::PaletteMid,                       QPalette::Mid,              QPalette::All,      false},
        {ColorRole::PaletteMidDisabled,               QPalette::Mid,              QPalette::Disabled, false},
        {ColorRole::PaletteShadow,                    QPalette::Shadow,           QPalette::All,      false},
        {ColorRole::PaletteShadowDisabled,            QPalette::Shadow,           QPalette::Disabled, false}
    };

    for (auto entry: mapping) {
        const QColor themeColor = color(entry.themeColor);
        // Use original color if color is not defined in theme.
        if (themeColor.isValid()) {
//            if (entry.setColorRoleAsBrush)
//                // TODO: Find out why sometimes setBrush is used
//                pal.setBrush(entry.paletteColorGroup, entry.paletteColorRole, themeColor);
//            else
//                pal.setColor(entry.paletteColorGroup, entry.paletteColorRole, themeColor);
            pal.setBrush(entry.paletteColorGroup, entry.paletteColorRole, themeColor);
            pal.setColor(entry.paletteColorGroup, entry.paletteColorRole, themeColor);
        }
    }

    for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        const QColor text = pal.color(group, QPalette::Text), base = pal.color(group, QPalette::Base);
        const QColor placeholder(qRound(text.red() * .7 + base.red() * .3), qRound(text.green() * .7 + base.green() * .3), qRound(text.blue() * .7 + base.blue() * .3));
        pal.setColor(group, QPalette::PlaceholderText, placeholder);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        pal.setColor(group, QPalette::Accent, pal.color(group, QPalette::Highlight));
#endif
    }
    return pal;
}

AppTheme::AppTheme(const QString &filename, ThemeType type, ThemeCategory category, QObject *parent):QObject(parent)
{
    mFilename = filename;
    mType = type;
    mCategory = category;
    QFile file(filename);
    if (!file.exists()) {
        throw FileError(tr("Theme file '%1' doesn't exist!")
                        .arg(filename));
    }
    if (file.open(QFile::ReadOnly)) {
        if (file.size() > 256 * 1024) throw FileError(tr("Theme file is too large (maximum 256 KB)."));
        QByteArray content = file.read(256 * 1024 + 1).trimmed();
        if (content.isEmpty() || content.size() > 256 * 1024 || file.error() != QFile::NoError)
            throw FileError(tr("Invalid or unreadable theme file: %1").arg(filename));
        QJsonObject obj;

        switch (type) {
        case ThemeType::HardCoded:
            __builtin_unreachable();
        case ThemeType::JSON: {
            QJsonParseError error;
            QJsonDocument doc(QJsonDocument::fromJson(content, &error));
            if (error.error  != QJsonParseError::NoError) {
                throw FileError(tr("Error in json file '%1':%2 : %3")
                                    .arg(filename)
                                    .arg(error.offset)
                                    .arg(error.errorString()));
            }
            if (!doc.isObject() || !doc.object().value("palette").isObject()) throw FileError(tr("Theme must contain a palette object."));
            obj = doc.object();

            // In Lua-based theme, the "style" key has replaced "isDark" and "useQtFusionStyle" keys.
            // The following part handles old "isDark" and "useQtFusionStyle" keys.
            if (!obj.contains("style")) {
                bool useQtFusionStyle = obj["useQtFusionStyle"].toBool(true);
                if (useQtFusionStyle) {
                    bool isDark = obj["isDark"].toBool(false);
                    obj["style"] = isDark ? "RedPandaDarkFusion" : "RedPandaLightFusion";
                } else {
                    obj["style"] = AppTheme::initialStyle();
                }
            }

            // In Lua-based theme, the script handles name localization.
            // The following part handles old "name_xx_XX" keys.
            QString localeName = obj["name_"+(pSettings ? pSettings->environment().language() : QLocale::system().name())].toString();
            if (!localeName.isEmpty())
                obj["name"] = localeName;

            break;
        }
#ifdef ENABLE_LUA_ADDON
        case ThemeType::Lua: {
            obj = AddOn::Lua::ThemeExecutor{}(content, filename);
            break;
        }
#endif
        }

        QFileInfo fileInfo(filename);
        mName = fileInfo.baseName();
        mDisplayName = obj["name"].toString();
        if (mDisplayName.isEmpty())
            mDisplayName = mName;
        mStyle = obj["style"].toString();
        mDefaultColorScheme = obj["default scheme"].toString();
        mDefaultIconSet = obj["default iconset"].toString();
        QJsonObject colors = obj["palette"].toObject();
        const QMetaObject &m = *metaObject();
        QMetaEnum e = m.enumerator(m.indexOfEnumerator("ColorRole"));
        for (int i = 0, total = e.keyCount(); i < total; ++i) {
            const QString key = QLatin1String(e.key(i));
            if (colors.contains(key)) {
                QString val=colors[key].toString();
                QColor color(val);
                if (!color.isValid()) throw FileError(tr("Invalid theme color: %1").arg(key));
                mColors.insert(e.value(i), color);
            }
        }

    } else {
        throw FileError(tr("Can't open the theme file '%1' for read.")
                        .arg(filename));
    }
}

bool AppTheme::isSystemInDarkMode() {
    // https://www.qt.io/blog/dark-mode-on-windows-11-with-qt-6.5
    // compare the window color with the text color to determine whether the palette is dark or light
    return initialPalette().color(QPalette::WindowText).lightness() > initialPalette().color(QPalette::Window).lightness();
}

// If you copy QPalette, default values stay at default, even if that default is different
// within the context of different widgets. Create deep copy.
static QPalette copyPalette(const QPalette &p)
{
    QPalette res;
    for (int group = 0; group < QPalette::NColorGroups; ++group) {
        for (int role = 0; role < QPalette::NColorRoles; ++role) {
            res.setBrush(QPalette::ColorGroup(group),
                         QPalette::ColorRole(role),
                         p.brush(QPalette::ColorGroup(group), QPalette::ColorRole(role)));
            res.setColor(QPalette::ColorGroup(group),
                         QPalette::ColorRole(role),
                         p.color(QPalette::ColorGroup(group), QPalette::ColorRole(role)));
        }
    }
    return res;
}

QPalette AppTheme::initialPalette()
{
    static QPalette palette = copyPalette(QApplication::palette());
    return palette;
}

AppTheme::ThemeCategory AppTheme::category() const
{
    return mCategory;
}

QString AppTheme::initialStyle()
{
    static QString style = QApplication::style()->objectName();
    return style;
}

const QString &AppTheme::filename() const
{
    return mFilename;
}

const QString AppTheme::categoryIcon() const
{
    switch (mCategory) {
    case ThemeCategory::FailSafe:
        return QString{"\U0000274C"}; //❌
    case ThemeCategory::BuiltIn:
        return QString{"\U0001F4E6"}; //📦
    case ThemeCategory::Custom:
        return  QString{"\U0001F4C4"}; //📄
    case ThemeCategory::Shared:
        return QString{"\U0001F310"}; //🌐
    default:
        return "";
    }
}

QString AppTheme::categoryLabel() const {
    switch (mCategory) {
    case ThemeCategory::FailSafe: return tr("Fallback");
    case ThemeCategory::BuiltIn: return tr("Built-in");
    case ThemeCategory::Custom: return tr("Custom");
    case ThemeCategory::Shared: return tr("Shared");
    }
    return QString();
}

QJsonObject AppTheme::toJson() const
{
    QJsonObject colors;
    const QMetaEnum roles = QMetaEnum::fromType<ColorRole>();
    for (int i = 0; i < roles.keyCount(); ++i) {
        const QColor value = color(static_cast<ColorRole>(roles.value(i)));
        if (value.isValid()) colors.insert(QString::fromLatin1(roles.key(i)), value.name(QColor::HexArgb));
    }
    return {{"name", mDisplayName}, {"style", mStyle}, {"default scheme", mDefaultColorScheme}, {"default iconset", mDefaultIconSet}, {"palette", colors}};
}

void ThemeManager::invalidateCache() { mCache.clear(); }
PAppTheme ThemeManager::loadCached(const QString &filename, AppTheme::ThemeType type, AppTheme::ThemeCategory category)
{
    const QFileInfo info(filename);
    const QString language = pSettings ? pSettings->environment().language() : QLocale::system().name();
    const QString key = filename + "|" + language + "|" + QString::number(info.size()) + "|" + QString::number(info.lastModified().toMSecsSinceEpoch());
    if (mCache.contains(key)) return mCache.value(key);
    auto theme = std::make_shared<AppTheme>(filename, type, category);
    if (mCache.size() >= 64) mCache.clear();
    mCache.insert(key, theme); return theme;
}

bool AppTheme::copyTo(const QString &targetFolder)
{
    QFile origin(mFilename);
    if (!origin.open(QIODevice::ReadOnly)) return false;
    const auto bytes = origin.read(256 * 1024 + 1);
    if (bytes.size() > 256 * 1024 || origin.error() != QFile::NoError) return false;
    QSaveFile target(QDir(targetFolder).filePath(QFileInfo(mFilename).fileName()));
    return target.open(QIODevice::WriteOnly) && target.write(bytes) == bytes.size() && target.commit();
}

const QString &AppTheme::defaultIconSet() const
{
    return mDefaultIconSet;
}

void AppTheme::setDefaultIconSet(const QString &newDefaultIconSet)
{
    mDefaultIconSet = newDefaultIconSet;
}

const QString &AppTheme::name() const
{
    return mName;
}

const QString &AppTheme::displayName() const
{
    return mDisplayName;
}

const QString &AppTheme::defaultColorScheme() const
{
    return mDefaultColorScheme;
}

void AppTheme::setDefaultColorScheme(const QString &newDefaultColorScheme)
{
    mDefaultColorScheme = newDefaultColorScheme;
}

const QString &AppTheme::style() const
{
    return mStyle;
}

AppTheme::AppTheme() :
    mName("__failsafe__theme__"),
    mDisplayName("Fusion"),
    mStyle("fusion"),
    mDefaultColorScheme("Adaptive"),
    mDefaultIconSet("newlook"),
    mType(ThemeType::HardCoded),
    mCategory(ThemeCategory::FailSafe)
{
}

PAppTheme AppTheme::fallbackTheme()
{
    static PAppTheme theme = PAppTheme(new AppTheme());
    return theme;
}
