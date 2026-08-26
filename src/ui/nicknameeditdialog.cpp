// 所有入口共用同一套清洗规则，避免不同界面产生不一致数据。
#include "ui/nicknameeditdialog.h"

#include <QInputDialog>
#include <QKeySequence>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QShortcut>
#include <QVBoxLayout>

#include "core/appstrings.h"
#include "core/nicknameutils.h"

namespace {
// 提供深色编辑器、输入框和按钮的一致视觉样式。
QString nicknameEditorStyleSheet()
{
    return QStringLiteral(
        "QDialog#nicknameEditor{background-color:rgb(32,32,36);color:#fff}"
        "QPlainTextEdit,QLineEdit{background-color:rgb(48,48,54);"
        "border:1px solid rgb(82,82,90);border-radius:7px;color:#fff;"
        "selection-background-color:rgb(0,120,215);padding:6px}"
        "QLabel{background:transparent;color:#fff}"
        "QPushButton{background-color:rgb(58,58,64);border:1px solid rgb(88,88,96);"
        "border-radius:7px;color:#fff;padding:6px 14px}"
        "QPushButton:hover{background-color:rgb(70,70,78)}"
        "QPushButton:pressed{background-color:rgb(46,46,52)}");
}

// 为对话框设置对象名、背景绘制和共享样式表。
void prepareEditorDialog(QDialog *dialog)
{
    dialog->setObjectName(QStringLiteral("nicknameEditor"));
    dialog->setAttribute(Qt::WA_StyledBackground);
    dialog->setStyleSheet(nicknameEditorStyleSheet());
}
}

// 显示多行编辑器，保存时清洗并检查大小写不敏感的重复 nickname。
QStringList NicknameEditDialog::multiLine(QWidget *parent, const QStringList &current)
{
    QDialog dialog(parent);
    prepareEditorDialog(&dialog);
    dialog.setWindowTitle(AppStrings::nicknameEditorTitle());
    auto *layout = new QVBoxLayout(&dialog);
    auto *editor = new QPlainTextEdit(current.join(QLatin1Char('\n')), &dialog);
    editor->setPlaceholderText(AppStrings::oneNicknamePerLine());
    auto *confirm = new QPushButton(AppStrings::editorSaveButton(), &dialog);
    auto *cancel = new QPushButton(AppStrings::editorCancelButton(), &dialog);
    layout->addWidget(editor);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(confirm);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);

    QStringList savedNicknames;
    // 保存回调只在输入合法时接受对话框，避免半成品覆盖原数据。
    const auto save = [&]() {
        const QStringList nicknames = NicknameUtils::clean(editor->toPlainText());
        QSet<QString> unique;
        bool duplicate = false;
        for (const QString &nickname : nicknames) {
            const QString normalized = NicknameUtils::normalize(nickname);
            if (unique.contains(normalized))
                duplicate = true;
            else
                unique.insert(normalized);
        }
        if (nicknames.isEmpty() || duplicate) {
            QMessageBox::warning(parent, AppStrings::applicationName(),
                                 AppStrings::invalidNicknamesMessage());
            return;
        }
        savedNicknames = nicknames;
        dialog.accept();
    };
    connect(confirm, &QPushButton::clicked, &dialog, save);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    auto *saveShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+S")), &dialog);
    connect(saveShortcut, &QShortcut::activated, &dialog, save);
    dialog.exec();
    return savedNicknames;
}

// 反复提示直到得到非空单行 nickname；取消直接返回空字符串。
QString NicknameEditDialog::singleLine(QWidget *parent, const QString &current)
{
    while (true) {
        QInputDialog dialog(parent);
        prepareEditorDialog(&dialog);
        dialog.setWindowTitle(AppStrings::nicknameEditorTitle());
        dialog.setLabelText(AppStrings::editSingleNicknameLabel());
        dialog.setInputMode(QInputDialog::TextInput);
        dialog.setTextEchoMode(QLineEdit::Normal);
        dialog.setTextValue(current);
        if (dialog.exec() != QDialog::Accepted)
            return {};
        const QString trimmed = dialog.textValue().trimmed();
        if (!trimmed.isEmpty())
            return trimmed;
    }
}
