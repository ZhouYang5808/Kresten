#include "newprojectwizard.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>

NewProjectWizard::NewProjectWizard(QWidget *parent)
    : QWizard(parent)
{
    setWindowTitle("New Kresten Project");
    setMinimumSize(600, 400);
    
    setPage(Page_Intro, new IntroPage(this));
    setPage(Page_Details, new ProjectDetailsPage(this));
    setPage(Page_Summary, new SummaryPage(this));
    
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoBackButtonOnStartPage, true);
    setOption(QWizard::NoCancelButtonOnLastPage, true);
}

QString NewProjectWizard::projectPath() const
{
    QWizardPage *page = page(Page_Details);
    ProjectDetailsPage *detailsPage = qobject_cast<ProjectDetailsPage*>(page);
    return detailsPage ? detailsPage->m_pathEdit->text() : QString();
}

QString NewProjectWizard::projectName() const
{
    QWizardPage *page = page(Page_Details);
    ProjectDetailsPage *detailsPage = qobject_cast<ProjectDetailsPage*>(page);
    return detailsPage ? detailsPage->m_nameEdit->text() : QString();
}

QString NewProjectWizard::projectType() const
{
    QWizardPage *page = page(Page_Details);
    ProjectDetailsPage *detailsPage = qobject_cast<ProjectDetailsPage*>(page);
    return detailsPage ? detailsPage->m_typeCombo->currentText() : "plugin";
}

// IntroPage
IntroPage::IntroPage(QWidget *parent)
    : QWizardPage(parent)
{
    setTitle("Welcome to Kresten Project Wizard");
    setSubTitle("Create a new plugin or driver project for Kresten OS");
    
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("This wizard will help you create a new Kresten OS project.\n"
                                 "You can create either a Plugin (.kplg) or a Driver (.kdrv).\n\n"
                                 "Plugins are user-space modules that run in the Kresten shell/desktop.\n"
                                 "Drivers are kernel-space modules that interact with hardware."));
}

// ProjectDetailsPage
ProjectDetailsPage::ProjectDetailsPage(QWidget *parent)
    : QWizardPage(parent)
{
    setTitle("Project Details");
    setSubTitle("Specify the project location, name, and type");
    
    QFormLayout *layout = new QFormLayout(this);
    
    m_pathEdit = new QLineEdit();
    m_pathEdit->setText(QDir::homePath() + "/KrestenProjects");
    QPushButton *browseBtn = new QPushButton("Browse...");
    connect(browseBtn, &QPushButton::clicked, [this](){
        QString dir = QFileDialog::getExistingDirectory(this, "Select Project Location", m_pathEdit->text());
        if (!dir.isEmpty()) m_pathEdit->setText(dir);
    });
    QHBoxLayout *pathLayout = new QHBoxLayout();
    pathLayout->addWidget(m_pathEdit);
    pathLayout->addWidget(new QPushButton("Browse...")); // placeholder
    layout->addRow("Location:", m_pathEdit);
    
    m_nameEdit = new QLineEdit();
    m_nameEdit->setPlaceholderText("my_plugin");
    connect(m_nameEdit, &QLineEdit::textChanged, this, &QWizardPage::completeChanged);
    layout->addRow("Project Name:", m_nameEdit);
    
    m_typeCombo = new QComboBox();
    m_typeCombo->addItem("Plugin (.kplg)", "plugin");
    m_typeCombo->addItem("Driver (.kdrv)", "driver");
    layout->addRow("Project Type:", m_typeCombo);
    
    setLayout(new QVBoxLayout());
    QVBoxLayout *vLayout = qobject_cast<QVBoxLayout*>(layout());
    vLayout->addLayout(layout);
    vLayout->addStretch();
    
    registerField("projectPath*", m_pathEdit);
    registerField("projectName*", m_nameEdit);
    registerField("projectType", m_typeCombo);
}

bool ProjectDetailsPage::validatePage()
{
    QString path = m_pathEdit->text();
    QString name = m_nameEdit->text();
    
    if (path.isEmpty() || name.isEmpty()) {
        return false;
    }
    
    QDir dir(path);
    if (!dir.exists()) {
        if (!QDir().mkpath(path)) return false;
    }
    
    QString projectDir = QDir(path).filePath(name);
    if (QDir(projectDir).exists()) {
        // Could ask user to overwrite
    }
    
    return true;
}

// SummaryPage
SummaryPage::SummaryPage(QWidget *parent)
    : QWizardPage(parent)
{
    setTitle("Summary");
    setSubTitle("Review your project settings");
    
    QVBoxLayout *layout = new QVBoxLayout(this);
    m_summaryLabel = new QLabel();
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setStyleSheet("font-family: monospace; padding: 10px; background: #2D2D2D; border-radius: 4px;");
    layout->addWidget(m_summaryLabel);
}

void SummaryPage::initializePage()
{
    QString path = field("projectPath").toString();
    QString name = field("projectName").toString();
    QString type = field("projectType").toString();
    
    QString summary = QString(
        "Project Location: %1\n"
        "Project Name: %2\n"
        "Project Type: %3 (%4)\n"
        "Full Path: %5"
    ).arg(path, name, type, (type == "plugin" ? ".kplg" : ".kdrv"),
          QDir(path).filePath(name));
    
    m_summaryLabel->setText(summary);
}