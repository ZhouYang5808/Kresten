#ifndef PROJECTMANAGER_H
#define PROJECTMANAGER_H

#include <QObject>
#include <QProcess>
#include <QDir>
#include <QFile>

class ProjectManager : public QObject
{
    Q_OBJECT

public:
    explicit ProjectManager(QObject *parent = nullptr);
    
    bool createProject(const QString &path, const QString &type, const QString &name);
    bool loadProject(const QString &path);
    void buildProject(const QString &projectPath);
    void cleanProject(const QString &projectPath);
    void runProject(const QString &projectPath);
    
    QString detectProjectType(const QString &path) const;
    QString getBuildCommand(const QString &projectPath, const QString &target) const;

signals:
    void projectLoaded(const QString &path);
    void fileOpened(const QString &path);
    void buildFinished(bool success, const QString &output);
    void buildProgress(const QString &output);

private:
    bool copyTemplate(const QString &srcDir, const QString &dstDir, const QString &projectName);
    QString generateCMakeLists(const QString &projectPath, const QString &projectType, const QString &projectName);
    QString getSdkRoot() const;
    
    QProcess *m_buildProcess;
    QString m_currentProjectPath;
    QString m_currentProjectType;
    QString m_currentProjectName;
};

#endif // PROJECTMANAGER_H