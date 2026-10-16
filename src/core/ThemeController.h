#pragma once

#include <QObject>
#include <QString>

class QSettings;

// 主题模式控制：深色 / 浅色 / 跟随系统 + 持久化。
//
// 只做「状态机 + 持久化」，不碰任何平台 API：
//   - 系统深浅由外部探测后经 setSystemDark() 注入（见 main.cpp detectSystemDark）；
//   - Qt Quick Controls 2 的调色板由 QML 侧绑定（见 Theme.qml 的 controls 表），
//     因为 Qt 5.15 的 Default 样式既不读 QGuiApplication 调色板，qApp->setPalette()
//     也推不动它（实测），这里就不做无用功。
// 因此本类不依赖 QGuiApplication，可用 QTEST_APPLESS_MAIN 直接单测。
class ThemeController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)
    Q_PROPERTY(bool systemDark READ systemDark WRITE setSystemDark NOTIFY systemDarkChanged)

public:
    static const char *modeAuto();
    static const char *modeLight();
    static const char *modeDark();

    // settings 为 nullptr 时用默认 QSettings（依赖 QCoreApplication 的
    // organizationName/applicationName，main.cpp 已设置）；传入时只借用不持有。
    explicit ThemeController(QSettings *settings = nullptr, QObject *parent = nullptr);

    QString mode() const;
    void setMode(const QString &mode);

    bool dark() const;
    bool systemDark() const;
    void setSystemDark(bool dark);

    // 浅色 <-> 深色（显式选择，写入配置）；「跟随系统」只经 setMode("auto") 进入。
    Q_INVOKABLE void toggle();

    // 从配置载入（构造时已自动调用一次）
    Q_INVOKABLE void load();
    Q_INVOKABLE void save() const;

signals:
    void modeChanged();
    void darkChanged();
    void systemDarkChanged();

private:
    // 规范化：非法值回退深色（应用默认观感）
    static QString normalizeMode(const QString &mode);

    QSettings *m_settings = nullptr;
    QString m_mode;
    bool m_systemDark = false;
};
