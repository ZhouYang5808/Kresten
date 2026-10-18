#ifndef PROJECTTREE_H
#define PROJECTTREE_H

#include <QTreeView>
#include <QFileSystemModel>
#include <QMenu>
#include <QAction>

class ProjectTree : public QTreeView
{
    Q_OBJECT

public:
    explicit ProjectTree(QWidget *parent = nullptr);
    
    void setProjectPath(const QString &path);
    QString projectPath() const;

signals:
    void fileDoubleClicked(const QString &path);
    void fileRightClicked(const QString &path, const QPoint &globalPos);

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private slots:
    void onNewFile();
    void onNewFolder();
    void onDelete();
    void onRename();
    void onBuildFile();

private:
    QFileSystemModel *m_model;
    QString m_projectPath;
    QMenu *m_contextMenu;
    QAction *m_newFileAction;
    QAction *m_newFolderAction;
    QAction *m_deleteAction;
    QAction *m_renameAction;
    QAction *m_buildFileAction;
};

#endif // PROJECTTREE_H