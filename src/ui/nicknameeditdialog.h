// 声明复用 nickname 清洗规则的单行和多行编辑对话框。
#ifndef UI_NICKNAMEEDITDIALOG_H
#define UI_NICKNAMEEDITDIALOG_H

// 统一复用多行与单行 nickname 编辑器。
#include <QDialog>
#include <QStringList>

class NicknameEditDialog : public QDialog
{
    Q_OBJECT
public:
    /// 显示多行 nickname 编辑器，返回清洗且去重通过的结果。
    static QStringList multiLine(QWidget *parent, const QStringList &current);
    /// 显示单行 nickname 编辑器，取消时返回空字符串。
    static QString singleLine(QWidget *parent, const QString &current);
};

#endif
