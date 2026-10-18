#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    
    QString sdkPath() const;
    QString mingwPath() const;
    QString qemuPath() const;
    int makeJobs() const;
    bool autoSave() const;
    bool showLineNumbers() const;

private:
    QLineEdit *m_sdkPathEdit;
    QLineEdit *m_mingwPathEdit;
    QLineEdit *m_qemuPathEdit;
    QSpinBox *m_makeJobsSpin;
    QCheckBox *m_autoSaveCheck;
    QCheckBox *m_lineNumbersCheck;
};

#endif // SETTINGSDIALOG_H