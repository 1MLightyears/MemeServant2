// API Key输入框只用于提交到Windows凭据管理器，不会写回配置文件。
#include "ui/settingswindow.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "app/appcontroller.h"
#include "core/appstrings.h"
#include "ui/appstyle.h"

// 创建各设置页并将控件变化连接到防抖保存计时器。
SettingsWindow::SettingsWindow(AppController *controller, QWidget *parent)
    : QMainWindow(parent), m_controller(controller)
{
    setWindowTitle(AppStrings::settingsWindowTitle());
    // 主题跟随系统配色；必须在构建页面前应用，保证所有控件一次到位。
    AppStyle::applyTheme();
    resize(620, 520);
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildGeneralPage(), AppStrings::generalPage());
    tabs->addTab(buildCapturePage(), AppStrings::capturePage());
    tabs->addTab(buildQuickbarPage(), AppStrings::quickbarPage());
    tabs->addTab(buildAiPage(), AppStrings::aiPage());
    tabs->addTab(buildStoragePage(), AppStrings::storagePage());
    tabs->addTab(buildAboutPage(), AppStrings::aboutPage());
    layout->addWidget(tabs);
    setCentralWidget(central);
    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setSingleShot(true);
    m_autoSaveTimer->setInterval(700);
    // 所有普通设置变化都进入同一个防抖保存入口。
    connect(m_autoSaveTimer, &QTimer::timeout, this, &SettingsWindow::save);
    connect(m_startup, &QCheckBox::toggled, this, [this]() { scheduleSave(); });
    connect(m_captureEnabled, &QCheckBox::toggled, this, [this]() { scheduleSave(); });
    connect(m_maxWidth, &QSpinBox::valueChanged, this, [this]() { scheduleSave(); });
    connect(m_maxHeight, &QSpinBox::valueChanged, this, [this]() { scheduleSave(); });
    connect(m_saveShortcut, &QKeySequenceEdit::keySequenceChanged, this, [this]() { scheduleSave(); });
    connect(m_cancelShortcut, &QKeySequenceEdit::keySequenceChanged, this, [this]() { scheduleSave(); });
    connect(m_aiShortcut, &QKeySequenceEdit::keySequenceChanged, this, [this]() { scheduleSave(); });
    connect(m_quickHotkey, &QKeySequenceEdit::keySequenceChanged, this, [this]() { scheduleSave(); });
    connect(m_rows, &QSpinBox::valueChanged, this, [this]() { scheduleSave(); });
    connect(m_columns, &QSpinBox::valueChanged, this, [this]() { scheduleSave(); });
    connect(m_displaySize, &QComboBox::currentIndexChanged, this, [this]() { scheduleSave(); });
    connect(m_autoPaste, &QCheckBox::toggled, this, [this]() { scheduleSave(); });
    connect(m_endpoint, &QLineEdit::textEdited, this, [this]() { scheduleSave(); });
    connect(m_model, &QLineEdit::textEdited, this, [this]() { scheduleSave(); });
    connect(m_apiEnvName, &QLineEdit::textEdited, this, [this]() { scheduleSave(); });
    connect(m_autoAi, &QCheckBox::toggled, this, [this]() { scheduleSave(); });
    connect(m_aiNicknameExampleCount, &QSpinBox::valueChanged, this, [this]() { scheduleSave(); });
    connect(m_galleryPath, &QLineEdit::editingFinished, this, [this]() { scheduleSave(); });
    connect(m_cacheSize, &QComboBox::currentIndexChanged, this, [this]() { scheduleSave(); });
    connect(m_controller, &AppController::configurationSaved, this, [this]() { reloadFromController(); });
    connect(m_controller, &AppController::importFinished, this, [this](const ImportResult &result) {
        QString message = AppStrings::importFinishedMessage(result.memes, result.nicknames);
        if (result.failed)
            message += AppStrings::importedFailures(result.failed);
        if (!result.error.isEmpty())
            QMessageBox::critical(this, AppStrings::importFinishedTitle(), result.error);
        else
            QMessageBox::information(this, AppStrings::importFinishedTitle(), message);
    });
    reloadFromController();
}

// 创建开机启动开关。
QWidget *SettingsWindow::buildGeneralPage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);
    m_startup = new QCheckBox(AppStrings::autoStartText(), page);
    form->addRow(m_startup);
    return page;
}

// 创建剪贴板监听、尺寸上限和捕获快捷键设置。
QWidget *SettingsWindow::buildCapturePage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);
    m_captureEnabled = new QCheckBox(AppStrings::listenClipboardText(), page);
    // QSpinBox自带整数校验：非数字直接拒绝，空值/越界在失焦时收敛到边界(16~16384)，天然只允许正整数。
    m_maxWidth = new QSpinBox(page); m_maxWidth->setRange(16, 16384);
    m_maxHeight = new QSpinBox(page); m_maxHeight->setRange(16, 16384);
    m_saveShortcut = new QKeySequenceEdit(page);
    m_cancelShortcut = new QKeySequenceEdit(page);
    m_aiShortcut = new QKeySequenceEdit(page);
    form->addRow(m_captureEnabled);
    // 分组框标题直接说明字段含义：剪贴板图片超过该长宽时跳过捕获；单位px放在输入框外。
    auto *sizeGroup = new QGroupBox(AppStrings::captureSizeGroupTitle(), page);
    auto *sizeRow = new QHBoxLayout(sizeGroup);
    auto *lengthLabel = new QLabel(AppStrings::maxWidthLabel(), sizeGroup);
    auto *widthLabel = new QLabel(AppStrings::maxHeightLabel(), sizeGroup);
    auto *lengthUnit = new QLabel(AppStrings::pixelUnitLabel(), sizeGroup);
    auto *widthUnit = new QLabel(AppStrings::pixelUnitLabel(), sizeGroup);
    sizeRow->addWidget(lengthLabel);
    sizeRow->addWidget(m_maxWidth);
    sizeRow->addWidget(lengthUnit);
    sizeRow->addSpacing(24);
    sizeRow->addWidget(widthLabel);
    sizeRow->addWidget(m_maxHeight);
    sizeRow->addWidget(widthUnit);
    sizeRow->addStretch();
    form->addRow(sizeGroup);
    form->addRow(AppStrings::saveShortcutLabel(), m_saveShortcut);
    form->addRow(AppStrings::cancelShortcutLabel(), m_cancelShortcut);
    form->addRow(AppStrings::aiShortcutLabel(), m_aiShortcut);
    return page;
}

// 创建快捷栏快捷键、网格尺寸、缩略图和自动粘贴设置。
QWidget *SettingsWindow::buildQuickbarPage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);
    m_quickHotkey = new QKeySequenceEdit(page);
    m_rows = new QSpinBox(page); m_rows->setRange(1, 8);
    m_columns = new QSpinBox(page); m_columns->setRange(1, 8);
    m_displaySize = new QComboBox(page);
    for (int size : {64, 96, 128, 192, 256}) m_displaySize->addItem(AppStrings::pixelSize(size), size);
    m_autoPaste = new QCheckBox(AppStrings::autoPasteText(), page);
    form->addRow(AppStrings::globalHotkeyLabel(), m_quickHotkey);
    form->addRow(AppStrings::candidateRowsLabel(), m_rows);
    form->addRow(AppStrings::candidateColumnsLabel(), m_columns);
    form->addRow(AppStrings::thumbnailDisplayLabel(), m_displaySize);
    form->addRow(m_autoPaste);
    return page;
}

// 创建 Provider、Endpoint、密钥来源和自动识图设置。
QWidget *SettingsWindow::buildAiPage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);
    m_provider = new QComboBox(page); m_provider->addItem(AppStrings::openAiCompatibleProvider());
    m_endpoint = new QLineEdit(page); m_endpoint->setPlaceholderText(AppStrings::exampleEndpoint());
    m_endpoint->setToolTip(AppStrings::endpointHint());
    m_model = new QLineEdit(page);
    m_apiKey = new QLineEdit(page); m_apiKey->setEchoMode(QLineEdit::Password);
    m_apiKey->setPlaceholderText(AppStrings::keepApiKeyPlaceholder());
    m_apiEnvName = new QLineEdit(page); m_apiEnvName->setPlaceholderText(AppStrings::apiKeyEnvExample());
    m_autoAi = new QCheckBox(AppStrings::autoAiText(), page);
    m_aiNicknameExampleCount = new QSpinBox(page);
    m_aiNicknameExampleCount->setRange(0, 50);
    m_aiNicknameExampleCount->setSuffix(QStringLiteral(" 个"));
    m_aiNicknameExampleCount->setSpecialValueText(QStringLiteral("关闭"));
    m_aiNicknameExampleCount->setToolTip(AppStrings::aiNicknameExampleCountHint());
    form->addRow(AppStrings::providerLabel(), m_provider);
    form->addRow(AppStrings::endpointLabel(), m_endpoint);
    form->addRow(AppStrings::modelLabel(), m_model);
    form->addRow(AppStrings::apiKeyLabel(), m_apiKey);
    form->addRow(AppStrings::apiKeyEnvLabel(), m_apiEnvName);
    form->addRow(AppStrings::aiNicknameExampleCountLabel(), m_aiNicknameExampleCount);
    form->addRow(m_autoAi);
    auto *keys = new QHBoxLayout;
    auto *saveKey = new QPushButton(AppStrings::saveApiKeyButton(), page);
    auto *clearKey = new QPushButton(AppStrings::clearApiKeyButton(), page);
    keys->addWidget(saveKey); keys->addWidget(clearKey);
    form->addRow(QString(), keys);
    connect(saveKey, &QPushButton::clicked, this, &SettingsWindow::saveApiKey);
    connect(clearKey, &QPushButton::clicked, this, &SettingsWindow::clearApiKey);
    return page;
}

// 创建图库路径、导入按钮和缩略图缓存设置。
QWidget *SettingsWindow::buildStoragePage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);
    m_galleryPath = new QLineEdit(page);
    m_galleryPath->setToolTip(AppStrings::galleryDirectoryHint());
    auto *galleryLabel = new QLabel(AppStrings::galleryDirectoryLabel(), page);
    galleryLabel->setToolTip(AppStrings::galleryDirectoryHint());
    galleryLabel->setBuddy(m_galleryPath);
    // 原两个长按钮改为小号方形图标按钮；原文案转为tooltip，圆角由QSS统一绘制。
    m_galleryBrowse = new QPushButton(page);
    m_galleryBrowse->setObjectName(QStringLiteral("galleryBrowse"));
    m_galleryBrowse->setToolTip(AppStrings::changeGalleryHint());
    m_importButton = new QPushButton(page);
    m_importButton->setObjectName(QStringLiteral("galleryImport"));
    m_importButton->setToolTip(AppStrings::importDataHint());
    for (QPushButton *button : {m_galleryBrowse, m_importButton}) {
        // 图标16px，按钮24px：比图标大一圈；与行内输入框高度一致，表单标签自然垂直居中。
        button->setProperty("iconButton", true);
        button->setFixedSize(24, 24);
        button->setIconSize(QSize(16, 16));
        button->setFocusPolicy(Qt::TabFocus);
    }
    refreshStorageIcons();
    m_cacheSize = new QComboBox(page);
    for (int size : {64, 128, 256, 512, 1024}) m_cacheSize->addItem(AppStrings::pixelSize(size), size);
    auto *pathRow = new QHBoxLayout;
    pathRow->setContentsMargins(0, 0, 0, 0);
    pathRow->addWidget(m_galleryPath, 1);
    pathRow->addWidget(m_galleryBrowse);
    pathRow->addWidget(m_importButton);
    form->addRow(galleryLabel, pathRow);
    form->addRow(AppStrings::thumbnailCacheLabel(), m_cacheSize);
    connect(m_galleryBrowse, &QPushButton::clicked, this, &SettingsWindow::chooseGallery);
    connect(m_importButton, &QPushButton::clicked, this, &SettingsWindow::importGallery);
    return page;
}

// 创建版本信息页。
QWidget *SettingsWindow::buildAboutPage()
{
    auto *page = new QLabel(AppStrings::aboutHtml(), this);
    page->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    page->setContentsMargins(24, 24, 24, 24);
    return page;
}

// 暂停自动保存信号后，把控制器配置完整映射到各个控件。
void SettingsWindow::reloadFromController()
{
    m_updating = true;
    const AppConfig config = m_controller->config();
    m_startup->setChecked(config.startup);
    m_captureEnabled->setChecked(config.captureEnabled);
    m_maxWidth->setValue(config.maxWidth);
    m_maxHeight->setValue(config.maxHeight);
    m_saveShortcut->setKeySequence(QKeySequence(config.saveShortcut));
    m_cancelShortcut->setKeySequence(QKeySequence(config.cancelShortcut));
    m_aiShortcut->setKeySequence(QKeySequence(config.aiShortcut));
    m_quickHotkey->setKeySequence(QKeySequence(config.quickHotkey));
    m_rows->setValue(config.rows); m_columns->setValue(config.columns);
    m_autoPaste->setChecked(config.autoPaste);
    int index = m_displaySize->findData(config.thumbnailDisplaySize);
    m_displaySize->setCurrentIndex(index < 0 ? 2 : index);
    m_endpoint->setText(config.aiEndpoint); m_model->setText(config.aiModel);
    m_apiEnvName->setText(config.apiKeyEnvName); m_autoAi->setChecked(config.autoAi);
    m_aiNicknameExampleCount->setValue(config.aiNicknameExampleCount);
    m_galleryPath->setText(m_controller->galleryPath());
    index = m_cacheSize->findData(config.thumbnailCacheSize);
    m_cacheSize->setCurrentIndex(index < 0 ? 2 : index);
    m_updating = false;
}

// 用户编辑后启动 700ms 防抖计时，避免每个按键都写配置。
void SettingsWindow::scheduleSave()
{
    if (!m_updating)
        m_autoSaveTimer->start();
}

// 从控件读取可编辑字段，并保留控制器中未在此页面展示的字段。
void SettingsWindow::collect(AppConfig &config)
{
    const AppConfig old = m_controller->config();
    config = old;
    config.startup = m_startup->isChecked();
    config.captureEnabled = m_captureEnabled->isChecked();
    config.maxWidth = m_maxWidth->value(); config.maxHeight = m_maxHeight->value();
    if (!m_saveShortcut->keySequence().isEmpty()) config.saveShortcut = m_saveShortcut->keySequence().toString();
    if (!m_cancelShortcut->keySequence().isEmpty()) config.cancelShortcut = m_cancelShortcut->keySequence().toString();
    if (!m_aiShortcut->keySequence().isEmpty()) config.aiShortcut = m_aiShortcut->keySequence().toString();
    config.quickHotkey = m_quickHotkey->keySequence().toString();
    config.rows = m_rows->value(); config.columns = m_columns->value();
    config.thumbnailDisplaySize = m_displaySize->currentData().toInt();
    config.autoPaste = m_autoPaste->isChecked();
    config.aiEndpoint = m_endpoint->text(); config.aiModel = m_model->text();
    config.apiKeyEnvName = m_apiEnvName->text(); config.autoAi = m_autoAi->isChecked();
    config.aiNicknameExampleCount = m_aiNicknameExampleCount->value();
    config.galleryPath = m_galleryPath->text();
    config.thumbnailCacheSize = m_cacheSize->currentData().toInt();
}

// 校验 AI Endpoint 后交给控制器原子应用；失败时保留窗口供用户修正。
bool SettingsWindow::save()
{
    if (m_updating)
        return true;
    AppConfig config;
    collect(config);
    QString error;
    // Endpoint和Model都填写时才校验格式；任一项留空都视为AI识图未配置。
    if (!config.aiEndpoint.trimmed().isEmpty() && !config.aiModel.trimmed().isEmpty() &&
        !config.aiEndpoint.contains(QLatin1String("/chat/completions"), Qt::CaseInsensitive)) {
        QMessageBox::warning(this, AppStrings::aiConfigurationError(),
                             AppStrings::endpointInvalidMessage());
        return false;
    }
    if (!m_controller->saveConfiguration(config, &error))
        QMessageBox::warning(this, AppStrings::savedSettingsTitle(), error);
    return error.isEmpty();
}

// 选择图库目录，并拒绝未经确认的非空目录。
void SettingsWindow::chooseGallery()
{
    const QString directory = QFileDialog::getExistingDirectory(this, AppStrings::changeGalleryHint(),
                                                                m_galleryPath->text());
    if (directory.isEmpty())
        return;
    QDir target(directory);
    const bool existingLibrary = target.exists(QStringLiteral("memeservant2.db"));
    if (!existingLibrary && target.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).count() > 0 &&
        QMessageBox::question(this, AppStrings::nonEmptyDirectoryTitle(),
                              AppStrings::nonEmptyDirectoryQuestion()) != QMessageBox::Yes)
        return;
    m_galleryPath->setText(QDir::toNativeSeparators(directory));
    scheduleSave();
}

// 选择 .db/.zip 文件并交给控制器异步导入。
void SettingsWindow::importGallery()
{
    const QString database = QFileDialog::getOpenFileName(this, AppStrings::selectSourceDatabaseTitle(),
                                                          QString(), AppStrings::sqliteFilter());
    if (!database.isEmpty())
        m_controller->importDatabase(database);
}

// 先保存普通配置，再将输入的 API Key 写入凭据管理器并清空输入框。
void SettingsWindow::saveApiKey()
{
    QString error;
    m_autoSaveTimer->stop();
    const bool settingsSaved = save();
    if (!settingsSaved)
        return;
    if (m_apiKey->text().trimmed().isEmpty())
        return;
    if (!m_controller->storeApiKey(m_apiKey->text(), &error)) {
        QMessageBox::warning(this, AppStrings::apiKeyTitle(), error);
        return;
    }
    AppConfig config = m_controller->config();
    config.hasStoredApiKey = !m_apiKey->text().trimmed().isEmpty();
    m_controller->saveConfiguration(config, &error);
    m_apiKey->clear();
    m_apiKey->setPlaceholderText(config.hasStoredApiKey ? AppStrings::storedApiKeyPlaceholder()
                                                        : AppStrings::keepApiKeyPlaceholder());
}

// 删除凭据管理器中的 API Key，并同步清除配置标志。
void SettingsWindow::clearApiKey()
{
    QString error;
    m_autoSaveTimer->stop();
    save();
    if (!m_controller->storeApiKey(QString(), &error)) {
        QMessageBox::warning(this, AppStrings::apiKeyTitle(), error);
        return;
    }
    AppConfig config = m_controller->config();
    config.hasStoredApiKey = false;
    m_controller->saveConfiguration(config, &error);
    m_apiKey->setPlaceholderText(AppStrings::keepApiKeyPlaceholder());
}

// 关闭前冲刷待保存配置；成功后隐藏窗口而不是退出应用。
void SettingsWindow::closeEvent(QCloseEvent *event)
{
    // 关闭前立即冲刷防抖；校验或保存失败时保持窗口打开并让用户处理提示。
    if (m_autoSaveTimer && m_autoSaveTimer->isActive()) {
        m_autoSaveTimer->stop();
        if (!save()) {
            event->ignore();
            raise();
            activateWindow();
            return;
        }
    }
    hide();
    event->ignore();
}

// 系统切换深浅色模式时，重新应用主题并用新前景色重绘SVG图标。
void SettingsWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ThemeChange) {
        AppStyle::applyTheme();
        refreshStorageIcons();
    }
    QMainWindow::changeEvent(event);
}

// 用当前配色方案的图标颜色刷新存储页的两个图标按钮。
void SettingsWindow::refreshStorageIcons()
{
    if (!m_galleryBrowse || !m_importButton)
        return;
    m_galleryBrowse->setIcon(AppStyle::themedIcon(QStringLiteral(":/icons/set.svg")));
    m_importButton->setIcon(AppStyle::themedIcon(QStringLiteral(":/icons/import.svg")));
}
