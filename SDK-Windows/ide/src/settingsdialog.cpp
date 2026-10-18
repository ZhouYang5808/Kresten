#include "settingsdialog.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QFileDialog>
#include <QSettings>
#include <QDialogButtonBox>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Settings");
    setMinimumWidth(500);
    
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    // Paths group
    QGroupBox *pathsGroup = new QGroupBox("Paths");
    QFormLayout *pathsLayout = new QFormLayout(pathsGroup);
    
    m_sdkPathEdit = new QLineEdit();
    m_sdkPathEdit->setPlaceholderText("Auto-detected");
    QPushButton *sdkBrowse = new QPushButton("Browse...");
    connect(sdkBrowse, &QPushButton::clicked, [this](){
        QString dir = QFileDialog::getExistingDirectory(this, "Select SDK Root", m_sdkPathEdit->text());
        if (!dir.isEmpty()) m_sdkPathEdit->setText(dir);
    });
    QHBoxLayout *sdkLayout = new QHBoxLayout();
    sdkLayout->addWidget(m_sdkPathEdit);
    sdkLayout->addWidget(sdkBrowse);
    pathsLayout->addRow("SDK Root:", sdkLayout);
    
    m_mingwPathEdit = new QLineEdit();
    m_mingwPathEdit->setPlaceholderText("Auto-detected from PATH");
    QPushButton *mingwBrowse = new QPushButton("Browse...");
    connect(mingwBrowse, &QPushButton::clicked, [this](){
        QString dir = QFileDialog::getExistingDirectory(this, "Select MinGW bin", m_mingwPathEdit->text());
        if (!dir.isEmpty()) m_mingwPathEdit->setText(dir);
    });
    QHBoxLayout *mingwLayout = new QHBoxLayout();
    mingwLayout->addWidget(m_mingwPathEdit);
    mingwLayout->addWidget(mingwBrowse);
    pathsLayout->addRow("MinGW bin:", mingwLayout);
    
    m_qemuPathEdit = new QLineEdit();
    m_qemuPathEdit->setPlaceholderText("qemu-system-x86_64 (from PATH)");
    QPushButton *qemuBrowse = new QPushButton("Browse...");
    connect(qemuBrowse, &QPushButton::clicked, [this](){
        QString file = QFileDialog::getOpenFileName(this, "Select QEMU", m_qemuPathEdit->text());
        if (!file.isEmpty()) m_qemuPathEdit->setText(file);
    });
    QHBoxLayout *qemuLayout = new QHBoxLayout();
    qemuLayout->addWidget(m_qemuPathEdit);
    qemuLayout->addWidget(qemuBrowse);
    pathsLayout->addRow("QEMU:", qemuLayout);
    
    mainLayout->addWidget(pathsGroup);
    
    // Build group
    QGroupBox *buildGroup = new QGroupBox("Build");
    QFormLayout *buildLayout = new QFormLayout(buildGroup);
    
    m_makeJobsSpin = new QSpinBox();
    m_makeJobsSpin->setRange(1, 64);
    m_makeJobsSpin->setValue(QThread::idealThreadCount());
    buildLayout->addRow("Parallel jobs:", m_makeJobsSpin);
    
    m_autoSaveCheck = new QCheckBox("Auto-save before build");
    m_autoSaveCheck->setChecked(true);
    buildLayout->addRow(m_autoSaveCheck);
    
    mainLayout->addWidget(buildGroup);
    
    // Editor group
    QGroupBox *editorGroup = new QGroupBox("Editor");
    QFormLayout *editorLayout = new QFormLayout(editorGroup);
    
    m_lineNumbersCheck = new QCheckBox("Show line numbers");
    m_lineNumbersCheck->setChecked(true);
    editorLayout->addRow(m_lineNumbersCheck);
    
    mainLayout->addWidget(editorGroup);
    
    mainLayout->addStretch();
    
    // Buttons
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);
    
    // Load settings
    loadSettings();
}

QString SettingsDialog::sdkPath() const
{
    return m_sdkPathEdit->text();
}

QString SettingsDialog::mingwPath() const
{
    return m_mingwPathEdit->text();
}

QString SettingsDialog::qemuPath() const
{
    return m_qemuPathEdit->text();
}

int SettingsDialog::makeJobs() const
{
    return m_makeJobsSpin->value();
}

bool SettingsDialog::autoSave() const
{
    return m_autoSaveCheck->isChecked();
}

bool SettingsDialog::showLineNumbers() const
{
    return m_lineNumbersCheck->isChecked();
}

void SettingsDialog::loadSettings()
{
    QSettings settings("Kresten", "KrestenIDE");
    m_sdkPathEdit->setText(settings.value("sdkPath").toString());
    m_mingwPathEdit->setText(settings.value("mingwPath").toString());
    m_qemuPathEdit->setText(settings.value("qemuPath").toString());
    m_makeJobsSpin->setValue(settings.value("makeJobs", QThread::idealThreadCount()).toInt());
    m_autoSaveCheck->setChecked(settings.value("autoSave", true).toBool());
    m_lineNumbersCheck->setChecked(settings.value("showLineNumbers", true).toBool());
}

void SettingsDialog::accept()
{
    QSettings settings("Kresten", "KrestenIDE");
    settings.setValue("sdkPath", m_sdkPathEdit->text());
    settings.setValue("mingwPath", m_mingwPathEdit->text());
    settings.setValue("qemuPath", m_qemuPathEdit->text());
    settings.setValue("makeJobs", m_makeJobsSpin->value());
    settings.setValue("autoSave", m_autoSaveCheck->isChecked());
    settings.setValue("showLineNumbers", m_lineNumbersCheck->isChecked());
    
    QDialog::accept();
}