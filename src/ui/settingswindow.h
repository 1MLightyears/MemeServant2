// 声明设置窗口及其分页面构建、保存和导入操作。
#ifndef UI_SETTINGSWINDOW_H
#define UI_SETTINGSWINDOW_H

// 轻量设置窗口，关闭仅隐藏，真正退出由托盘菜单控制。
#include <QMainWindow>
#include "storage/appconfig.h"

class QLineEdit;
class QSpinBox;
class QCheckBox;
class QKeySequenceEdit;
class QComboBox;
class QTimer;

class AppController;

class SettingsWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit SettingsWindow(AppController *controller, QWidget *parent = nullptr);
    /// 从控制器重新读取配置并更新所有控件。
    void reloadFromController();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    bool save();
    void chooseGallery();
    void importGallery();
    void saveApiKey();
    void clearApiKey();

private:
    /// 构建常规设置页。
    QWidget *buildGeneralPage();
    /// 构建图片捕获设置页。
    QWidget *buildCapturePage();
    /// 构建快捷栏设置页。
    QWidget *buildQuickbarPage();
    /// 构建 AI 识图设置页。
    QWidget *buildAiPage();
    /// 构建存储、导入和缩略图设置页。
    QWidget *buildStoragePage();
    /// 构建版本信息页。
    QWidget *buildAboutPage();
    /// 从控件收集配置，保留未在界面展示的字段。
    void collect(AppConfig &config);

    AppController *m_controller = nullptr;
    QCheckBox *m_startup = nullptr;
    QCheckBox *m_captureEnabled = nullptr;
    QSpinBox *m_maxWidth = nullptr;
    QSpinBox *m_maxHeight = nullptr;
    QKeySequenceEdit *m_saveShortcut = nullptr;
    QKeySequenceEdit *m_cancelShortcut = nullptr;
    QKeySequenceEdit *m_aiShortcut = nullptr;
    QKeySequenceEdit *m_quickHotkey = nullptr;
    QSpinBox *m_rows = nullptr;
    QSpinBox *m_columns = nullptr;
    QComboBox *m_displaySize = nullptr;
    QCheckBox *m_autoPaste = nullptr;
    QComboBox *m_provider = nullptr;
    QLineEdit *m_endpoint = nullptr;
    QLineEdit *m_model = nullptr;
    QLineEdit *m_apiKey = nullptr;
    QLineEdit *m_apiEnvName = nullptr;
    QCheckBox *m_autoAi = nullptr;
    QLineEdit *m_galleryPath = nullptr;
    QComboBox *m_cacheSize = nullptr;
    QTimer *m_autoSaveTimer = nullptr;
    bool m_updating = false;

    void scheduleSave();
};

#endif
