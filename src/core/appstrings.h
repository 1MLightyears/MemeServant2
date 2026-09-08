// 声明集中管理的主要用户可见文案生成函数。
#ifndef CORE_APPSTRINGS_H
#define CORE_APPSTRINGS_H

// 集中管理所有用户可见文案，便于统一修改措辞和后续接入Qt翻译。
#include <QString>

namespace AppStrings {
QString applicationName();
QString startupFailureTitle();
QString alreadyRunning();

QString settingsMenu();
QString pauseCaptureMenu();
QString resumeCaptureMenu();
QString exitMenu();
QString readyTooltip();
QString readyNotification();
QString databaseErrorTitle();
QString databaseErrorAdvice();
QString globalHotkeyUnavailable();

QString captureTitle();
QString nicknamePlaceholder(const QString &saveShortcut, const QString &cancelShortcut,
                            const QString &aiShortcut);
QString thumbnailUnavailable();
QString aiPreparing();
QString aiSending();
QString aiRecognizing();
QString aiProcessing();
QString aiFailed();
QString saveButton(const QString &shortcut);
QString cancelButton(const QString &shortcut);
QString saveShortcutCaption();
QString cancelShortcutCaption();

QString searchPlaceholder();
QString addNicknameAction();
QString editNicknamesAction();
QString modifyNicknamesAction();
QString manageOneNicknameMenu();
QString editNicknameAction();
QString deleteNicknameAction();
QString deleteMemeAction();
QString duplicateNicknameMessage();
QString lastNicknameMessage();
QString deleteMemeTitle();
QString deleteMemeQuestion();
QString missingMemeText();

QString nicknameEditorTitle();
QString oneNicknamePerLine();
QString editorSaveButton();
QString editorCancelButton();
QString invalidNicknamesMessage();
QString editSingleNicknameLabel();

QString settingsWindowTitle();
QString generalPage();
QString capturePage();
QString quickbarPage();
QString aiPage();
QString storagePage();
QString aboutPage();
QString autoStartText();
QString listenClipboardText();
QString captureSizeGroupTitle();
QString maxWidthLabel();
QString maxHeightLabel();
QString saveShortcutLabel();
QString cancelShortcutLabel();
QString aiShortcutLabel();
QString globalHotkeyLabel();
QString candidateRowsLabel();
QString candidateColumnsLabel();
QString thumbnailDisplayLabel();
QString autoPasteText();
QString providerLabel();
QString endpointLabel();
QString modelLabel();
QString apiKeyLabel();
QString apiKeyEnvLabel();
QString autoAiText();
QString aiNicknameExampleCountLabel();
QString aiNicknameExampleCountHint();
QString endpointHint();
QString keepApiKeyPlaceholder();
 QString storedApiKeyPlaceholder();
QString clearApiKeyButton();
QString saveApiKeyButton();
QString galleryDirectoryLabel();
QString changeGalleryButton();
QString importDataButton();
QString thumbnailCacheLabel();
QString pixelSize(int value);
QString pixelUnitLabel();
QString aboutHtml();
QString aiConfigurationError();
QString endpointInvalidMessage();
QString modelRequiredMessage();
QString savedSettingsTitle();
QString nonEmptyDirectoryTitle();
QString nonEmptyDirectoryQuestion();
QString selectSourceDatabaseTitle();
QString sqliteFilter();
QString aiNotConfigured();
QString apiKeyTitle();
QString importFinishedTitle();
QString importFinishedMessage(int memes, int nicknames);
QString importedFailures(int count);
QString apiKeyMissingMessage();
QString aiImageInvalidMessage();
QString aiPromptUnavailableMessage();
QString aiHttpFailedMessage(int status);
QString aiTimeoutMessage();
QString aiNicknameEmptyMessage();
QString saveFailedTitle();
QString copyFailedTitle();
QString originalImageMissing();
QString originalImageReadFailed(const QString &detail);
QString originalImageDecodeFailed();
QString galleryCreateFailed();
QString sqliteOpenFailed(const QString &detail);
QString databaseInitializeFailed(const QString &detail);
QString missingSchemaVersion();
QString migrationBackupFailed();
QString migrationFailed(const QString &detail);
QString saveMemeFailed();
QString nicknameSaveFailed();
QString deleteRecordFailed();
QString openAiCompatibleProvider();
QString exampleEndpoint();
QString apiKeyEnvExample();
QString galleryNotWritable();
QString configWriteFailed(const QString &detail);
QString originalImageSaveFailed();
}

#endif
