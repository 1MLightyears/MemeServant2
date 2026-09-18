// 集中定义主要用户可见文案，避免界面层重复拼接相同提示。
#include "core/appstrings.h"

// 产品名和版本来自 CMake 生成的 appmetadata.h。
#include <appmetadata.h>

QString AppStrings::applicationName() { return QStringLiteral(MEMESERVANT2_DISPLAY_NAME); }
QString AppStrings::startupFailureTitle() { return QStringLiteral("MemeServant2 启动失败"); }
QString AppStrings::alreadyRunning() { return QStringLiteral("MemeServant2已在运行。"); }

QString AppStrings::settingsMenu() { return QStringLiteral("设置"); }
QString AppStrings::pauseCaptureMenu() { return QStringLiteral("暂停图片捕获"); }
QString AppStrings::resumeCaptureMenu() { return QStringLiteral("恢复图片捕获"); }
QString AppStrings::exitMenu() { return QStringLiteral("退出"); }
QString AppStrings::readyTooltip() { return QStringLiteral(MEMESERVANT2_DISPLAY_NAME " v" MEMESERVANT2_VERSION_STRING); }
QString AppStrings::readyNotification() { return QStringLiteral(MEMESERVANT2_DISPLAY_NAME " v" MEMESERVANT2_VERSION_STRING); }
QString AppStrings::databaseErrorTitle() { return QStringLiteral("数据库错误"); }
QString AppStrings::databaseErrorAdvice() { return QStringLiteral("\n可以打开图库目录检查数据库，或退出应用。"); }
QString AppStrings::globalHotkeyUnavailable() { return QStringLiteral("MemeServant2 全局快捷键不可用，请在设置中重新配置。"); }

QString AppStrings::captureTitle() { return QStringLiteral("保存为表情包"); }
QString AppStrings::nicknamePlaceholder(const QString &saveShortcut, const QString &cancelShortcut,
                                        const QString &aiShortcut)
{
    return QStringLiteral("输入 nickname，每行一个\n%1 保存  ·  %2 取消  ·  %3 AI识图")
        .arg(saveShortcut, cancelShortcut, aiShortcut);
}
QString AppStrings::thumbnailUnavailable() { return QStringLiteral("无法预览这张图片"); }
QString AppStrings::aiPreparing() { return QStringLiteral("正在准备图片……"); }
QString AppStrings::aiSending() { return QStringLiteral("正在发送图片……"); }
QString AppStrings::aiRecognizing() { return QStringLiteral("AI正在识图……"); }
QString AppStrings::aiProcessing() { return QStringLiteral("正在处理识图结果……"); }
QString AppStrings::aiFailed() { return QStringLiteral("AI识别失败"); }
QString AppStrings::saveButton(const QString &shortcut) { return QStringLiteral("保存 (%1)").arg(shortcut); }
QString AppStrings::cancelButton(const QString &shortcut) { return QStringLiteral("取消 (%1)").arg(shortcut); }
QString AppStrings::saveShortcutCaption() { return QStringLiteral("Ctrl+S"); }
QString AppStrings::cancelShortcutCaption() { return QStringLiteral("Esc"); }

QString AppStrings::searchPlaceholder() { return QStringLiteral("搜索nickname……（Enter 选择，Esc 关闭）"); }
QString AppStrings::addNicknameAction() { return QStringLiteral("添加nickname"); }
QString AppStrings::editNicknamesAction() { return QStringLiteral("编辑nickname"); }
QString AppStrings::manageOneNicknameMenu() { return QStringLiteral("管理单个nickname"); }
QString AppStrings::modifyNicknamesAction() { return QStringLiteral("修改nicknames..."); }
QString AppStrings::editNicknameAction() { return QStringLiteral("编辑nickname"); }
QString AppStrings::deleteNicknameAction() { return QStringLiteral("删除nickname"); }
QString AppStrings::deleteMemeAction() { return QStringLiteral("删除表情包"); }
QString AppStrings::duplicateNicknameMessage() { return QStringLiteral("同一表情包内nickname不能重复。"); }
QString AppStrings::lastNicknameMessage() { return QStringLiteral("不能删除最后一个nickname。"); }
QString AppStrings::deleteMemeTitle() { return QStringLiteral("删除表情包"); }
QString AppStrings::deleteMemeQuestion() { return QStringLiteral("将同时删除原图、缩略图和全部nickname，确定继续吗？"); }
QString AppStrings::missingMemeText() { return QStringLiteral("表情包找不到了……_(:з)∠)_"); }

QString AppStrings::nicknameEditorTitle() { return QStringLiteral("编辑nickname"); }
QString AppStrings::oneNicknamePerLine() { return QStringLiteral("每行一个nickname；删除某行即删除该nickname"); }
QString AppStrings::editorSaveButton() { return QStringLiteral("保存 (Ctrl+S)"); }
QString AppStrings::editorCancelButton() { return QStringLiteral("取消 (Esc)"); }
QString AppStrings::invalidNicknamesMessage() { return QStringLiteral("至少需要一个nickname，且同一表情包内不能重复。"); }
QString AppStrings::editSingleNicknameLabel() { return QStringLiteral("nickname"); }

QString AppStrings::settingsWindowTitle() { return QStringLiteral("MemeServant2 设置"); }
QString AppStrings::generalPage() { return QStringLiteral("常规"); }
QString AppStrings::capturePage() { return QStringLiteral("捕获"); }
QString AppStrings::quickbarPage() { return QStringLiteral("快捷栏"); }
QString AppStrings::aiPage() { return QStringLiteral("AI识图"); }
QString AppStrings::storagePage() { return QStringLiteral("存储"); }
QString AppStrings::aboutPage() { return QStringLiteral("关于"); }
QString AppStrings::autoStartText() { return QStringLiteral("开机启动"); }
QString AppStrings::thumbnailPreviewDelayLabel() { return QStringLiteral("悬停预览延迟"); }
QString AppStrings::thumbnailPreviewDelayHint()
{
    return QStringLiteral("鼠标在候选缩略图上停留该时长后放大显示原图；设为0秒可关闭预览。");
}
QString AppStrings::listenClipboardText() { return QStringLiteral("监听剪贴板新图片"); }
QString AppStrings::captureSizeGroupTitle() { return QStringLiteral("不捕获超过此尺寸的图片"); }
QString AppStrings::maxWidthLabel() { return QStringLiteral("长"); }
QString AppStrings::maxHeightLabel() { return QStringLiteral("宽"); }
QString AppStrings::saveShortcutLabel() { return QStringLiteral("保存快捷键"); }
QString AppStrings::cancelShortcutLabel() { return QStringLiteral("取消快捷键"); }
QString AppStrings::aiShortcutLabel() { return QStringLiteral("AI快捷键"); }
QString AppStrings::globalHotkeyLabel() { return QStringLiteral("全局快捷键"); }
QString AppStrings::candidateRowsLabel() { return QStringLiteral("候选行数"); }
QString AppStrings::candidateColumnsLabel() { return QStringLiteral("候选列数"); }
QString AppStrings::thumbnailDisplayLabel() { return QStringLiteral("缩略图显示尺寸"); }
QString AppStrings::autoPasteText() { return QStringLiteral("选中表情包后自动粘贴"); }
QString AppStrings::providerLabel() { return QStringLiteral("Provider"); }
QString AppStrings::endpointLabel() { return QStringLiteral("API Endpoint"); }
QString AppStrings::modelLabel() { return QStringLiteral("Model"); }
QString AppStrings::apiKeyLabel() { return QStringLiteral("API_KEY"); }
QString AppStrings::apiKeyEnvLabel() { return QStringLiteral("API_ENV_KEY"); }
QString AppStrings::autoAiText() { return QStringLiteral("总是将捕获图片发送给AI识图"); }
QString AppStrings::aiNicknameExampleCountLabel() { return QStringLiteral("Nickname风格示例数"); }
QString AppStrings::aiNicknameExampleCountHint()
{
    return QStringLiteral("每次请求从已有nickname中随机选择；设为0表示不使用风格示例。");
}
QString AppStrings::endpointHint() { return QStringLiteral("结尾应该是一个带 /chat/completions 的URL。"); }
QString AppStrings::keepApiKeyPlaceholder() { return QStringLiteral("留空表示不修改已保存密钥"); }
QString AppStrings::storedApiKeyPlaceholder() { return QStringLiteral("已保存在Windows凭据管理器"); }
QString AppStrings::clearApiKeyButton() { return QStringLiteral("清除API_KEY"); }
QString AppStrings::saveApiKeyButton() { return QStringLiteral("保存API_KEY"); }
QString AppStrings::galleryDirectoryLabel() { return QStringLiteral("当前表情包目录"); }
QString AppStrings::galleryDirectoryHint() { return QStringLiteral("memeservant2.db所在的目录"); }
QString AppStrings::changeGalleryHint() { return QStringLiteral("修改图库路径"); }
QString AppStrings::importDataHint() { return QStringLiteral("导入MemeServant2数据"); }
QString AppStrings::thumbnailCacheLabel() { return QStringLiteral("缩略图缓存最长边"); }
QString AppStrings::pixelSize(int value) { return QStringLiteral("%1 px").arg(value); }
QString AppStrings::pixelUnitLabel() { return QStringLiteral("px"); }
// 组合关于页 HTML；名称和版本由 CMake 配置生成，Qt 版本在运行时读取。
QString AppStrings::aboutHtml()
{
    return QStringLiteral("<h3>" MEMESERVANT2_DISPLAY_NAME "</h3><div>快捷检索、发送、管理你的表情包！</div><p>Version " MEMESERVANT2_VERSION_STRING
                          "<br>Qt %1<br>Windows x64</p>").arg(QT_VERSION_STR);
}
QString AppStrings::aiConfigurationError() { return QStringLiteral("AI配置错误"); }
QString AppStrings::endpointInvalidMessage() { return QStringLiteral("Endpoint无效：结尾应该是一个带 /chat/completions 的URL。"); }
QString AppStrings::modelRequiredMessage() { return QStringLiteral("Model不能为空。"); }
QString AppStrings::savedSettingsTitle() { return QStringLiteral("保存设置"); }
QString AppStrings::nonEmptyDirectoryTitle() { return QStringLiteral("目标目录非空"); }
QString AppStrings::nonEmptyDirectoryQuestion() { return QStringLiteral("所选目录不是空的。继续初始化并保留原有文件吗？"); }
QString AppStrings::selectSourceDatabaseTitle() { return QStringLiteral("选择源数据库"); }
QString AppStrings::sqliteFilter() { return QStringLiteral("MemeServant2 数据 (*.db *.zip)"); }
QString AppStrings::aiNotConfigured() { return QStringLiteral("AI识图未配置完成，请先填写Endpoint和Model。"); }
QString AppStrings::apiKeyTitle() { return QStringLiteral("API_KEY"); }
QString AppStrings::importFinishedTitle() { return QStringLiteral("导入完成"); }
// 生成导入统计摘要；失败数量由调用方另行追加。
QString AppStrings::importFinishedMessage(int memes, int nicknames)
{
    return QStringLiteral("已导入 %1 个表情包、%2 个nickname。").arg(memes).arg(nicknames);
}
QString AppStrings::importedFailures(int count) { return QStringLiteral("\n失败：%1 项。").arg(count); }
QString AppStrings::apiKeyMissingMessage() { return QStringLiteral("API_KEY或API_ENV_KEY至少需要配置一个。"); }
QString AppStrings::aiImageInvalidMessage() { return QStringLiteral("图片为空或超出AI请求大小限制。"); }
QString AppStrings::aiPromptUnavailableMessage() { return QStringLiteral("AI识图提示词资源无法读取。"); }
QString AppStrings::aiHttpFailedMessage(int status) { return QStringLiteral("AI HTTP请求失败（状态 %1）。").arg(status); }
QString AppStrings::aiTimeoutMessage() { return QStringLiteral("AI请求已超时（30秒）。"); }
QString AppStrings::aiNicknameEmptyMessage() { return QStringLiteral("AI返回的nickname为空。"); }
QString AppStrings::saveFailedTitle() { return QStringLiteral("保存失败"); }
QString AppStrings::copyFailedTitle() { return QStringLiteral("复制表情包失败"); }
QString AppStrings::originalImageMissing() { return QStringLiteral("数据库记录对应的原图不存在。"); }
// 将底层文件错误包装成用户可理解的原图读取提示。
QString AppStrings::originalImageReadFailed(const QString &detail)
{
    return QStringLiteral("无法读取表情包原图：%1").arg(detail);
}
QString AppStrings::originalImageDecodeFailed() { return QStringLiteral("表情包原图无法解码。"); }
QString AppStrings::galleryCreateFailed() { return QStringLiteral("图库目录创建失败，请检查存储位置权限。"); }
// 将 SQLite 驱动返回的具体原因附加到数据库打开提示。
QString AppStrings::sqliteOpenFailed(const QString &detail) { return QStringLiteral("数据库无法打开：%1").arg(detail); }
// 将 schema 初始化失败原因附加到迁移提示。
QString AppStrings::databaseInitializeFailed(const QString &detail) { return QStringLiteral("数据库初始化失败：%1").arg(detail); }
QString AppStrings::missingSchemaVersion() { return QStringLiteral("数据库缺少schema版本信息，为避免损坏已停止启动。"); }
QString AppStrings::migrationBackupFailed() { return QStringLiteral("迁移前数据库备份失败。"); }
// 将事务或 SQL 错误附加到 schema 迁移提示。
QString AppStrings::migrationFailed(const QString &detail) { return QStringLiteral("Schema迁移失败：%1").arg(detail); }
QString AppStrings::saveMemeFailed() { return QStringLiteral("保存表情包记录失败。"); }
QString AppStrings::nicknameSaveFailed() { return QStringLiteral("nickname保存失败，可能存在重复名称。"); }
QString AppStrings::deleteRecordFailed() { return QStringLiteral("删除数据库记录失败。"); }
QString AppStrings::openAiCompatibleProvider() { return QStringLiteral("OpenAI Compatible"); }
QString AppStrings::exampleEndpoint() { return QStringLiteral("https://api.example.com/v1/chat/completions"); }
QString AppStrings::apiKeyEnvExample() { return QStringLiteral("OPENAI_API_KEY"); }
QString AppStrings::galleryNotWritable() { return QStringLiteral("图库目录不可写，请修改存储位置。"); }
// 将文件系统错误附加到配置保存提示。
QString AppStrings::configWriteFailed(const QString &detail) { return QStringLiteral("配置文件写入失败：%1").arg(detail); }
QString AppStrings::originalImageSaveFailed() { return QStringLiteral("图库目录不可写或原图保存失败。"); }
