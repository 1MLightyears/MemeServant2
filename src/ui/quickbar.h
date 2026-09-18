// 声明快捷栏候选网格、搜索、预览和 nickname 管理交互。
#ifndef UI_QUICKBAR_H
#define UI_QUICKBAR_H

// 快捷栏提供二维候选、模糊搜索、预览、右键管理和键盘导航。
#include <QFrame>
#include <QTimer>
#include <QVector>
#include "core/models.h"
#include "core/searchengine.h"
#include "storage/appconfig.h"

class QLineEdit;
class QGridLayout;
class PreviewPopup;

class QuickCandidate;

class QuickBar : public QFrame
{
    Q_OBJECT
public:
    explicit QuickBar(QWidget *parent = nullptr);
    /// 替换搜索索引并刷新当前候选卡片。
    void updateRecords(const QVector<MemeRecord> &memes,
                       const QHash<QString, QVector<NicknameRecord>> &nicknames);
    /// 在指定前台窗口附近显示快捷栏并开始接收键盘导航。
    void openAt(const AppConfig &config, quintptr previousForeground);
    /// 保存尺寸、行列数、粘贴行为等配置快照，供后续重建候选界面。
    void applyConfig(const AppConfig &config);

signals:
    void memeSelected(const QString &memeId);
    void nicknamesChanged(const QString &memeId, const QStringList &nicknames);
    void memeDeleted(const QString &memeId);

protected:
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    /// 根据搜索词重建候选卡片和缩略图。
    void rebuild(const QString &query);
    /// 更新当前选中卡片并控制预览计时器。
    void setCurrent(int index);
    /// 将当前卡片写回剪贴板并按配置执行自动粘贴。
    void confirmCurrent();
    /// 显示候选卡片的 nickname 编辑和删除菜单。
    void showContextMenu(QuickCandidate *candidate, const QPoint &globalPosition);
    /// 返回表情包原图的绝对路径。
    QString sourcePath(const MemeRecord &record) const;
    /// 返回表情包缩略图缓存的绝对路径。
    QString thumbnailPath(const MemeRecord &record) const;

    AppConfig m_config;
    SearchEngine m_engine;
    QVector<SearchResult> m_results;
    QVector<QuickCandidate *> m_cards;
    QLineEdit *m_search = nullptr;
    QWidget *m_candidateHost = nullptr;
    QGridLayout *m_grid = nullptr;
    QTimer m_previewTimer;
    PreviewPopup *m_preview = nullptr;
    int m_current = 0;
    quintptr m_foregroundWindow = 0;
    bool m_managementActive = false;
    bool m_previewAllowed = false;
};

#endif
