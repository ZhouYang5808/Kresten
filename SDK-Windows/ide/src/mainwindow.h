#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSplitter>
#include <QTabWidget>
#include <QToolBar>
#include <QStatusBar>

class ProjectTree;
class CodeEditor;
class BuildOutput;
class ProjectManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void newProject();
    void openProject();
    void saveFile();
    void saveAllFiles();
    void buildProject();
    void cleanProject();
    void runProject();
    void openSettings();
    void onProjectLoaded(const QString &path);
    void onFileOpened(const QString &path);
    void onBuildFinished(bool success, const QString &output);
    void updateWindowTitle();

private:
    void setupUI();
    void setupToolBars();
    void setupMenus();
    void setupConnections();
    void loadSettings();
    void saveSettings();

    ProjectManager *m_projectManager;
    ProjectTree *m_projectTree;
    QTabWidget *m_editorTabs;
    BuildOutput *m_buildOutput;
    QSplitter *m_mainSplitter;
    QSplitter *m_editorSplitter;
    
    QToolBar *m_fileToolBar;
    QToolBar *m_buildToolBar;
    QToolBar *m_debugToolBar;
    
    QAction *m_newProjectAction;
    QAction *m_openProjectAction;
    QAction *m_saveAction;
    QAction *m_saveAllAction;
    QAction *m_buildAction;
    QAction *m_cleanAction;
    QAction *m_runAction;
    QAction *m_settingsAction;
    
    QString m_currentProjectPath;
    bool m_hasUnsavedChanges;
};

#endif // MAINWINDOW_H