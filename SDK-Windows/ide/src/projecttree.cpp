#include "projecttree.h"
#include <QFileSystemModel>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>

ProjectTree::ProjectTree(QWidget *parent)
    : QTreeView(parent)
    , m_model(new QFileSystemModel(this))
    , m_contextMenu(new QMenu(this))
{
    m_model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    m_model->setNameFilterDisables(false);
    setModel(m_model);
    
    // Hide columns except name
    for (int i = 1; i < m_model->columnCount(); ++i) {
        hideColumn(i);
    }
    
    setHeaderHidden(true);
    setAnimated(true);
    setIndentation(20);
    setSortingEnabled(true);
    sortByColumn(0, Qt::AscendingOrder);
    
    // Context menu
    m_newFileAction = m_contextMenu->addAction("New File");
    m_newFolderAction = m_contextMenu->addAction("New Folder");
    m_contextMenu->addSeparator();
    m_renameAction = m_contextMenu->addAction("Rename");
    m_deleteAction = m_contextMenu->addAction("Delete");
    m_contextMenu->addSeparator();
    m_buildFileAction = m_contextMenu->addAction("Build This File");
    
    connect(m_newFileAction, &QAction::triggered, this, &ProjectTree::onNewFile);
    connect(m_newFolderAction, &QAction::triggered, this, &ProjectTree::onNewFolder);
    connect(m_deleteAction, &QAction::triggered, this, &ProjectTree::onDelete);
    connect(m_renameAction, &QAction::triggered, this, &ProjectTree::onRename);
    connect(m_buildFileAction, &QAction::triggered, this, &ProjectTree::onBuildFile);
    
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos){
        QModelIndex index = indexAt(pos);
        if (index.isValid()) {
            m_contextMenu->exec(viewport()->mapToGlobal(pos));
        }
    });
}

void ProjectTree::setProjectPath(const QString &path)
{
    m_projectPath = path;
    m_model->setRootPath(path);
    setRootIndex(m_model->index(path));
    expandToDepth(2);
}

QString ProjectTree::projectPath() const
{
    return m_projectPath;
}

void ProjectTree::mouseDoubleClickEvent(QMouseEvent *event)
{
    QModelIndex index = indexAt(event->pos());
    if (index.isValid() && m_model->isDir(index) == false) {
        QString path = m_model->filePath(index);
        emit fileDoubleClicked(path);
    }
    QTreeView::mouseDoubleClickEvent(event);
}

void ProjectTree::contextMenuEvent(QContextMenuEvent *event)
{
    QModelIndex index = indexAt(event->pos());
    if (index.isValid()) {
        m_contextMenu->exec(event->globalPos());
    } else {
        QTreeView::contextMenuEvent(event);
    }
}

void ProjectTree::onNewFile()
{
    QModelIndex index = currentIndex();
    QString dir = m_model->isDir(index) ? m_model->filePath(index) : m_model->filePath(index.parent());
    
    bool ok;
    QString name = QInputDialog::getText(this, "New File", "File name:", QLineEdit::Normal, "new.c", &ok);
    if (ok && !name.isEmpty()) {
        QString filePath = QDir(dir).filePath(name);
        QFile file(filePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.close();
        }
    }
}

void ProjectTree::onNewFolder()
{
    QModelIndex index = currentIndex();
    QString dir = m_model->isDir(index) ? m_model->filePath(index) : m_model->filePath(index.parent());
    
    bool ok;
    QString name = QInputDialog::getText(this, "New Folder", "Folder name:", QLineEdit::Normal, "new_folder", &ok);
    if (ok && !name.isEmpty()) {
        QDir(dir).mkdir(name);
    }
}

void ProjectTree::onDelete()
{
    QModelIndex index = currentIndex();
    if (!index.isValid()) return;
    
    QString path = m_model->filePath(index);
    QFileInfo info(path);
    
    if (QMessageBox::question(this, "Delete", 
        QString("Delete %1?").arg(info.fileName()),
        QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        if (info.isDir()) {
            QDir(path).removeRecursively();
        } else {
            QFile(path).remove();
        }
    }
}

void ProjectTree::onRename()
{
    QModelIndex index = currentIndex();
    if (!index.isValid()) return;
    
    QString oldPath = m_model->filePath(index);
    QFileInfo info(oldPath);
    
    bool ok;
    QString newName = QInputDialog::getText(this, "Rename", "New name:", QLineEdit::Normal, info.fileName(), &ok);
    if (ok && !newName.isEmpty() && newName != info.fileName()) {
        QString newPath = QDir(info.absolutePath()).filePath(newName);
        QFile::rename(oldPath, newPath);
    }
}

void ProjectTree::onBuildFile()
{
    QModelIndex index = currentIndex();
    if (!index.isValid() || m_model->isDir(index)) return;
    
    QString path = m_model->filePath(index);
    emit fileDoubleClicked(path); // Trigger build for single file
}