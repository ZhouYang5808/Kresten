#include "mainwindow.h"
#include "projecttree.h"
#include "codeeditor.h"
#include "buildoutput.h"
#include "projectmanager.h"
#include "settingsdialog.h"
#include "newprojectwizard.h"

#include <QSplitter>
#include <QTabWidget>
#include <QToolBar>
#include <QStatusBar>
#include <QMenuBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QCloseEvent>
#include <QVBoxLayout>
#include <QLabel>
#include <QAction>
#include <QIcon>
#include <QShortcut>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_projectManager(new ProjectManager(this))
    , m_currentProjectPath("")
    , m_hasUnsavedChanges(false)
{
    setupUI();
    setupToolBars();
    setupMenus();
    setupConnections();
    loadSettings();
    
    setWindowTitle("Kresten IDE");
    resize(1400, 900);
}

MainWindow::~MainWindow()
{
    saveSettings();
}

void MainWindow::setupUI()
{
    // Main splitter (vertical: editor area + build output)
    m_mainSplitter = new QSplitter(Qt::Vertical, this);
    setCentralWidget(m_mainSplitter);
    
    // Editor area splitter (horizontal: project tree + editor tabs)
    m_editorSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter->addWidget(m_editorSplitter);
    
    // Project tree
    m_projectTree = new ProjectTree(this);
    m_editorSplitter->addWidget(m_projectTree);
    
    // Editor tabs
    m_editorTabs = new QTabWidget(this);
    m_editorTabs->setTabsClosable(true);
    m_editorTabs->setMovable(true);
    m_editorTabs->setDocumentMode(true);
    m_editorSplitter->addWidget(m_editorTabs);
    
    // Build output
    m_buildOutput = new BuildOutput(this);
    m_mainSplitter->addWidget(m_buildOutput);
    
    // Set splitter sizes
    m_editorSplitter->setSizes({250, 850});
    m_mainSplitter->setSizes({600, 200});
    
    // Status bar
    statusBar()->showMessage("Ready");
    
    // Welcome tab
    QWidget *welcomeWidget = new QWidget();
    QVBoxLayout *welcomeLayout = new QVBoxLayout(welcomeWidget);
    welcomeLayout->setAlignment(Qt::AlignCenter);
    
    QLabel *welcomeLabel = new QLabel("Welcome to Kresten IDE");
    welcomeLabel->setAlignment(Qt::AlignCenter);
    welcomeLabel->setStyleSheet("font-size: 24px; font-weight: bold; color: #4282DA; margin-bottom: 20px;");
    welcomeLayout->addWidget(welcomeLabel);
    
    QLabel *subtitleLabel = new QLabel("Kresten OS Plugin & Driver Development Environment");
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setStyleSheet("font-size: 14px; color: #CCCCCC;");
    welcomeLayout->addWidget(subtitleLabel);
    
    QLabel *actionsLabel = new QLabel("Create a new project or open an existing one to get started");
    actionsLabel->setAlignment(Qt::AlignCenter);
    actionsLabel->setStyleSheet("font-size: 12px; color: #888888; margin-top: 10px;");
    welcomeLayout->addWidget(actionsLabel);
    
    m_editorTabs->addTab(welcomeWidget, "Welcome");
}

void MainWindow::setupToolBars()
{
    // File toolbar
    m_fileToolBar = addToolBar("File");
    m_fileToolBar->setIconSize(QSize(24, 24));
    m_fileToolBar->setMovable(false);
    
    m_newProjectAction = m_fileToolBar->addAction(QIcon::fromTheme("document-new"), "New Project");
    m_newProjectAction->setShortcut(QKeySequence::New);
    m_fileToolBar->addAction(m_newProjectAction);
    
    m_openProjectAction = m_fileToolBar->addAction(QIcon::fromTheme("document-open"), "Open Project");
    m_openProjectAction->setShortcut(QKeySequence::Open);
    m_fileToolBar->addAction(m_openProjectAction);
    
    m_fileToolBar->addSeparator();
    
    m_saveAction = m_fileToolBar->addAction(QIcon::fromTheme("document-save"), "Save");
    m_saveAction->setShortcut(QKeySequence::Save);
    m_fileToolBar->addAction(m_saveAction);
    
    m_saveAllAction = m_fileToolBar->addAction(QIcon::fromTheme("document-save-all"), "Save All");
    m_saveAllAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    m_fileToolBar->addAction(m_saveAllAction);
    
    // Build toolbar
    m_buildToolBar = addToolBar("Build");
    m_buildToolBar->setIconSize(QSize(24, 24));
    m_buildToolBar->setMovable(false);
    
    m_buildAction = m_buildToolBar->addAction(QIcon::fromTheme("system-run"), "Build");
    m_buildAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    m_buildToolBar->addAction(m_buildAction);
    
    m_cleanAction = m_buildToolBar->addAction(QIcon::fromTheme("edit-clear"), "Clean");
    m_cleanAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    m_buildToolBar->addAction(m_cleanAction);
    
    m_runAction = m_buildToolBar->addAction(QIcon::fromTheme("media-playback-start"), "Run in QEMU");
    m_runAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    m_buildToolBar->addAction(m_runAction);
    
    // Debug toolbar
    m_debugToolBar = addToolBar("Debug");
    m_debugToolBar->setIconSize(QSize(24, 24));
    m_debugToolBar->setMovable(false);
    
    m_settingsAction = m_debugToolBar->addAction(QIcon::fromTheme("preferences-system"), "Settings");
    m_settingsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    m_debugToolBar->addAction(m_settingsAction);
}

void MainWindow::setupMenus()
{
    // File menu
    QMenu *fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction(m_newProjectAction);
    fileMenu->addAction(m_openProjectAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(m_saveAllAction);
    fileMenu->addSeparator();
    fileMenu->addAction("Exit", this, &QWidget::close, QKeySequence::Quit);
    
    // Build menu
    QMenu *buildMenu = menuBar()->addMenu("&Build");
    buildMenu->addAction(m_buildAction);
    buildMenu->addAction(m_cleanAction);
    buildMenu->addSeparator();
    buildMenu->addAction(m_runAction);
    
    // Tools menu
    QMenu *toolsMenu = menuBar()->addMenu("&Tools");
    toolsMenu->addAction(m_settingsAction);
    
    // View menu
    QMenu *viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction("Project Explorer", m_projectTree, &QWidget::show);
    viewMenu->addAction("Build Output", m_buildOutput, &QWidget::show);
    
    // Help menu
    QMenu *helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("About Kresten IDE", [](){
        QMessageBox::about(nullptr, "About Kresten IDE",
            "Kresten IDE 1.0.0\n\n"
            "Kresten OS Plugin & Driver Development Environment\n\n"
            "Supports building .kplg plugins and .kdrv drivers\n"
            "using MinGW-w64 cross-compiler.");
    });
}

void MainWindow::setupConnections()
{
    connect(m_newProjectAction, &QAction::triggered, this, &MainWindow::newProject);
    connect(m_openProjectAction, &QAction::triggered, this, &MainWindow::openProject);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveFile);
    connect(m_saveAllAction, &QAction::triggered, this, &MainWindow::saveAllFiles);
    connect(m_buildAction, &QAction::triggered, this, &MainWindow::buildProject);
    connect(m_cleanAction, &QAction::triggered, this, &MainWindow::cleanProject);
    connect(m_runAction, &QAction::triggered, this, &MainWindow::runProject);
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::openSettings);
    
    connect(m_projectManager, &ProjectManager::projectLoaded, this, &MainWindow::onProjectLoaded);
    connect(m_projectManager, &ProjectManager::fileOpened, this, &MainWindow::onFileOpened);
    connect(m_projectManager, &ProjectManager::buildFinished, this, &MainWindow::onBuildFinished);
    
    connect(m_editorTabs, &QTabWidget::tabCloseRequested, this, [this](int index){
        if (index == 0 && m_editorTabs->tabText(index) == "Welcome") return;
        QWidget *widget = m_editorTabs->widget(index);
        if (CodeEditor *editor = qobject_cast<CodeEditor*>(widget)) {
            if (editor->isModified()) {
                QMessageBox::StandardButton btn = QMessageBox::question(this, "Save Changes",
                    "Save changes to " + editor->fileName() + "?",
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
                if (btn == QMessageBox::Save) {
                    editor->save();
                } else if (btn == QMessageBox::Cancel) {
                    return;
                }
            }
        }
        m_editorTabs->removeTab(index);
        widget->deleteLater();
    });
    
    connect(m_editorTabs, &QTabWidget::currentChanged, this, &MainWindow::updateWindowTitle);
    
    connect(m_projectTree, &ProjectTree::fileDoubleClicked, this, &MainWindow::onFileOpened);
    
    // Shortcuts
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_W), this, [this](){
        int index = m_editorTabs->currentIndex();
        if (index >= 0) m_editorTabs->tabCloseRequested(index);
    });
}

void MainWindow::newProject()
{
    NewProjectWizard wizard(this);
    if (wizard.exec() == QDialog::Accepted) {
        QString projectPath = wizard.projectPath();
        QString projectType = wizard.projectType();
        QString projectName = wizard.projectName();
        
        if (m_projectManager->createProject(projectPath, projectType, projectName)) {
            m_projectManager->loadProject(projectPath);
        }
    }
}

void MainWindow::openProject()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Open Project", 
        m_currentProjectPath.isEmpty() ? QDir::homePath() : m_currentProjectPath);
    if (!dir.isEmpty()) {
        m_projectManager->loadProject(dir);
    }
}

void MainWindow::saveFile()
{
    int index = m_editorTabs->currentIndex();
    if (index > 0) {
        if (CodeEditor *editor = qobject_cast<CodeEditor*>(m_editorTabs->widget(index))) {
            editor->save();
        }
    }
}

void MainWindow::saveAllFiles()
{
    for (int i = 1; i < m_editorTabs->count(); ++i) {
        if (CodeEditor *editor = qobject_cast<CodeEditor*>(m_editorTabs->widget(i))) {
            if (editor->isModified()) {
                editor->save();
            }
        }
    }
}

void MainWindow::buildProject()
{
    if (m_currentProjectPath.isEmpty()) {
        QMessageBox::warning(this, "No Project", "Please open or create a project first.");
        return;
    }
    
    m_buildOutput->clear();
    m_buildOutput->appendLine("Building project...", BuildOutput::Info);
    m_projectManager->buildProject(m_currentProjectPath);
    statusBar()->showMessage("Building...");
}

void MainWindow::cleanProject()
{
    if (m_currentProjectPath.isEmpty()) return;
    
    m_buildOutput->clear();
    m_buildOutput->appendLine("Cleaning project...", BuildOutput::Info);
    m_projectManager->cleanProject(m_currentProjectPath);
    statusBar()->showMessage("Cleaning...");
}

void MainWindow::runProject()
{
    if (m_currentProjectPath.isEmpty()) {
        QMessageBox::warning(this, "No Project", "Please open or create a project first.");
        return;
    }
    
    m_buildOutput->appendLine("Running in QEMU...", BuildOutput::Info);
    m_projectManager->runProject(m_currentProjectPath);
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(this);
    dialog.exec();
}

void MainWindow::onProjectLoaded(const QString &path)
{
    m_currentProjectPath = path;
    m_projectTree->setProjectPath(path);
    setWindowTitle(QString("Kresten IDE - %1").arg(QDir(path).dirName()));
    statusBar()->showMessage("Project loaded: " + path);
}

void MainWindow::onFileOpened(const QString &path)
{
    // Check if already open
    for (int i = 1; i < m_editorTabs->count(); ++i) {
        if (CodeEditor *editor = qobject_cast<CodeEditor*>(m_editorTabs->widget(i))) {
            if (editor->filePath() == path) {
                m_editorTabs->setCurrentIndex(i);
                return;
            }
        }
    }
    
    // Open new file
    CodeEditor *editor = new CodeEditor(this);
    if (editor->openFile(path)) {
        int index = m_editorTabs->addTab(editor, QFileInfo(path).fileName());
        m_editorTabs->setCurrentIndex(index);
    } else {
        editor->deleteLater();
    }
}

void MainWindow::onBuildFinished(bool success, const QString &output)
{
    m_buildOutput->appendLine(output, success ? BuildOutput::Success : BuildOutput::Error);
    statusBar()->showMessage(success ? "Build successful" : "Build failed");
}

void MainWindow::updateWindowTitle()
{
    if (m_currentProjectPath.isEmpty()) {
        setWindowTitle("Kresten IDE");
    } else {
        QString projectName = QDir(m_currentProjectPath).dirName();
        setWindowTitle(QString("Kresten IDE - %1").arg(projectName));
    }
}

void MainWindow::loadSettings()
{
    QSettings settings("Kresten", "KrestenIDE");
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
    m_editorSplitter->restoreState(settings.value("editorSplitterState").toByteArray());
    m_mainSplitter->restoreState(settings.value("mainSplitterState").toByteArray());
    
    QString lastProject = settings.value("lastProject").toString();
    if (!lastProject.isEmpty() && QDir(lastProject).exists()) {
        m_projectManager->loadProject(lastProject);
    }
}

void MainWindow::saveSettings()
{
    QSettings settings("Kresten", "KrestenIDE");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    settings.setValue("editorSplitterState", m_editorSplitter->saveState());
    settings.setValue("mainSplitterState", m_mainSplitter->saveState());
    settings.setValue("lastProject", m_currentProjectPath);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Check for unsaved files
    bool hasUnsaved = false;
    for (int i = 1; i < m_editorTabs->count(); ++i) {
        if (CodeEditor *editor = qobject_cast<CodeEditor*>(m_editorTabs->widget(i))) {
            if (editor->isModified()) {
                hasUnsaved = true;
                break;
            }
        }
    }
    
    if (hasUnsaved) {
        QMessageBox::StandardButton btn = QMessageBox::question(this, "Unsaved Changes",
            "There are unsaved changes. Save before closing?",
            QMessageBox::SaveAll | QMessageBox::Discard | QMessageBox::Cancel);
        if (btn == QMessageBox::SaveAll) {
            saveAllFiles();
        } else if (btn == QMessageBox::Cancel) {
            event->ignore();
            return;
        }
    }
    
    saveSettings();
    event->accept();
}