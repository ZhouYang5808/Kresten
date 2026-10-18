#ifndef NEWPROJECTWIZARD_H
#define NEWPROJECTWIZARD_H

#include <QWizard>
#include <QLineEdit>
#include <QComboBox>
#include <QRadioButton>

class NewProjectWizard : public QWizard
{
    Q_OBJECT

public:
    explicit NewProjectWizard(QWidget *parent = nullptr);
    
    QString projectPath() const;
    QString projectName() const;
    QString projectType() const;

private:
    QLineEdit *m_pathEdit;
    QLineEdit *m_nameEdit;
    QComboBox *m_typeCombo;
};

class IntroPage : public QWizardPage
{
    Q_OBJECT
public:
    IntroPage(QWidget *parent = nullptr);
};

class ProjectDetailsPage : public QWizardPage
{
    Q_OBJECT
public:
    ProjectDetailsPage(QWidget *parent = nullptr);
    bool validatePage() override;
    
    QLineEdit *m_pathEdit;
    QLineEdit *m_nameEdit;
    QComboBox *m_typeCombo;
};

class SummaryPage : public QWizardPage
{
    Q_OBJECT
public:
    SummaryPage(QWidget *parent = nullptr);
    void initializePage() override;
    
    QLabel *m_summaryLabel;
};

#endif // NEWPROJECTWIZARD_H