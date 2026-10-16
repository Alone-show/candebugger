#include "core/ThemeController.h"

#include <QSettings>
#include <QStringList>

namespace {

// 配置键：QSettings 里存字符串，便于用户手改 / 打包脚本预置
const char *kModeKey = "ui/themeMode";

} // namespace

const char *ThemeController::modeAuto() { return "auto"; }
const char *ThemeController::modeLight() { return "light"; }
const char *ThemeController::modeDark() { return "dark"; }

ThemeController::ThemeController(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings ? settings : new QSettings(this))
{
    load();
}

QString ThemeController::normalizeMode(const QString &mode)
{
    const QString m = mode.trimmed().toLower();
    if (m == QLatin1String(modeAuto()) || m == QLatin1String(modeLight())
            || m == QLatin1String(modeDark()))
        return m;
    // 非法值（手改配置写错 / 配置损坏）回退默认深色
    return QString::fromLatin1(modeDark());
}

QString ThemeController::mode() const
{
    return m_mode;
}

void ThemeController::setMode(const QString &mode)
{
    const QString next = normalizeMode(mode);
    if (next == m_mode)
        return;
    const bool wasDark = dark();
    m_mode = next;
    save();
    emit modeChanged();
    if (dark() != wasDark)
        emit darkChanged();
}

bool ThemeController::dark() const
{
    if (m_mode == QLatin1String(modeDark()))
        return true;
    if (m_mode == QLatin1String(modeLight()))
        return false;
    return m_systemDark; // auto
}

bool ThemeController::systemDark() const
{
    return m_systemDark;
}

void ThemeController::setSystemDark(bool dark)
{
    if (dark == m_systemDark)
        return;
    const bool wasDark = this->dark();
    m_systemDark = dark;
    emit systemDarkChanged();
    // 只有 auto 模式下系统深浅才会影响最终观感
    if (this->dark() != wasDark)
        emit darkChanged();
}

void ThemeController::toggle()
{
    setMode(dark() ? QLatin1String(modeLight()) : QLatin1String(modeDark()));
}

void ThemeController::load()
{
    m_mode = normalizeMode(m_settings->value(QLatin1String(kModeKey)).toString());
}

void ThemeController::save() const
{
    m_settings->setValue(QLatin1String(kModeKey), m_mode);
}
